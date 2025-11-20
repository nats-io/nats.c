package main

import (
	"bufio"
	"context"
	"encoding/base64"
	"encoding/json"
	"flag"
	"fmt"
	"os"
	"strings"

	"github.com/nats-io/nats.go"
	"github.com/nats-io/nats.go/jetstream"
)

func main() {
	var (
		url      string
		user     string
		password string
		help     bool
	)

	flag.StringVar(&url, "url", "nats://127.0.0.1:4222", "Server url")
	flag.StringVar(&user, "user", "", "Username")
	flag.StringVar(&password, "password", "", "Username")
	flag.BoolVar(&help, "help", false, "Show this help")

	flag.Parse()

	if help {
		flag.Usage()
		os.Exit(1)
	}

	fmt.Println("")
	fmt.Println("!!! WARNING !!!")
	fmt.Println("")
	fmt.Println("You MUST stop any application that may be accessing the object stores while")
	fmt.Println("this tool is running. Also, it is strongly recommended to backup the object")
	fmt.Println("store streams before proceeding. If the tool fails, it will then be possible")
	fmt.Println("to delete the original stream(s) and restore it(them). This all can be done")
	fmt.Println("with the `nats` CLI tool (see `stream backup` and `stream restore` commands).")
	fmt.Println("")
	fmt.Print("Confirm object stores are not being used and backups made? [y/N]: ")

	reader := bufio.NewReader(os.Stdin)
	text, _ := reader.ReadString('\n')
	text = strings.TrimSuffix(text, "\n")
	if text != "y" && text != "Y" {
		fmt.Println("Exiting without fixing object stores!")
		os.Exit(1)
	}

	fmt.Println("")

	var opts []nats.Option
	if user != "" {
		opts = append(opts, nats.UserInfo(user, password))
	}
	nc, err := nats.Connect(url, opts...)
	if err != nil {
		fmt.Printf("Unable to connect: %v\n", err)
		os.Exit(1)
	}
	defer nc.Close()

	js, err := jetstream.New(nc)
	if err != nil {
		fmt.Printf("Unable to get JetStream context: %v\n", err)
		os.Exit(1)
	}

	var (
		fixed        int
		ctx          = context.Background()
		objectStores = js.ObjectStores(ctx)
	)
	for info := range objectStores.Status() {
		storeName := info.Bucket()
		fmt.Printf("Fixing object store %q\n", storeName)
		n, err := fixStore(ctx, js, storeName)
		if err != nil {
			if n > 0 {
				fmt.Printf(" => fixed %d entries, but got error: %v\n", n, err)
			} else {
				fmt.Printf(" => error: %v\n", err)
			}
			fmt.Println("")
			os.Exit(1)
		}
		if n > 0 {
			fmt.Printf(" => fixed %d entries\n", n)
			fixed += n
		} else {
			fmt.Println(" => no error found!")
		}
		fmt.Println("")
	}
	fmt.Printf("\nFixed a total of %v entries!", fixed)
}

func fixStore(ctx context.Context, js jetstream.JetStream, storeName string) (int, error) {
	streamName := fmt.Sprintf("OBJ_%s", storeName)
	stream, err := js.Stream(ctx, streamName)
	if err != nil {
		return 0, fmt.Errorf("unable to get stream %q: %v", streamName, err)
	}

	badOnes, mr, err := collectMetaRecords(ctx, js, storeName)
	if err != nil {
		return 0, err
	}
	if badOnes == 0 {
		return 0, nil
	}

	isSealed := stream.CachedInfo().Config.Sealed
	if isSealed {
		return fixSealedStore(ctx, js, stream, storeName, mr)
	}

	metaSubjPrexix := fmt.Sprintf("$O.%s.M.", storeName)

	var fixed int
	for _, m := range mr {
		if !m.bad && fixed == 0 {
			continue
		}
		correctSubj := metaSubjPrexix + m.enc
		correctMsg := nats.NewMsg(correctSubj)
		correctMsg.Header = m.msg.Headers()
		correctMsg.Data = m.msg.Data()
		if _, err := js.PublishMsg(ctx, correctMsg); err != nil {
			return fixed, fmt.Errorf("unable to write into %q: %v", correctSubj, err)
		}
		if m.bad {
			if err = stream.DeleteMsg(ctx, m.seq); err != nil {
				return fixed, fmt.Errorf("unable to delete message sequence %v: %v", m.seq, err)
			}
			// We count as "fixed" only the ones that really had bad encoding.
			fixed++
		}
	}
	return fixed, nil
}

func fixSealedStore(ctx context.Context, js jetstream.JetStream, stream jetstream.Stream,
	storeName string, metaRecords []*metaRec) (int, error) {

	tmpStoreName := storeName + "_fix"
	tmpStreamName := "OBJ_" + tmpStoreName
	tmpChunksSubj := fmt.Sprintf("$O.%s.C.>", tmpStoreName)
	tmpMetaSubj := fmt.Sprintf("$O.%s.M.>", tmpStoreName)

	// Create the temporary stream. We use current config and "undo" config
	// changes made by the server when sealing a stream.
	cfg := stream.CachedInfo().Config
	cfg.Sealed = false
	cfg.DenyDelete, cfg.DenyPurge = false, false
	cfg.AllowRollup, cfg.AllowDirect = true, true
	cfg.Name = tmpStreamName
	cfg.Subjects = []string{tmpChunksSubj, tmpMetaSubj}
	_, err := js.CreateStream(ctx, cfg)
	if err != nil {
		return 0, fmt.Errorf("unable to create stream %q: %v", tmpStreamName, err)
	}

	// Do a first pass where we are going to transfer all chunks to the temp stream.
	chunkPrefix := fmt.Sprintf("$O.%s.C.", storeName)
	cons, err := stream.OrderedConsumer(ctx, jetstream.OrderedConsumerConfig{
		FilterSubjects: []string{chunkPrefix + ">"},
	})
	if err != nil {
		return 0, fmt.Errorf("unable to create consumer for chunks: %v", err)
	}
	defer stream.DeleteConsumer(ctx, cons.CachedInfo().Name)

	tmpChunkPrefix := fmt.Sprintf("$O.%s.C.", tmpStoreName)
	for range cons.CachedInfo().NumPending {
		msg, err := cons.Next()
		if err != nil {
			return 0, fmt.Errorf("unable to get next chunk: %v", err)
		}
		nuid := strings.TrimPrefix(msg.Subject(), chunkPrefix)
		if nuid == "" {
			return 0, fmt.Errorf("invalid original chunk subject %q", msg.Subject())
		}
		tmpMsg := nats.NewMsg(tmpChunkPrefix + nuid)
		tmpMsg.Header = msg.Headers()
		tmpMsg.Data = msg.Data()
		if _, err := js.PublishMsg(ctx, tmpMsg); err != nil {
			return 0, fmt.Errorf("unable to write into %q: %v", tmpMsg.Subject, err)
		}
	}

	tmpMetaPrefix := fmt.Sprintf("$O.%s.M.", tmpStoreName)
	var fixed int
	for _, m := range metaRecords {
		if !m.bad && fixed == 0 {
			continue
		}
		correctSubj := tmpMetaPrefix + m.enc
		correctMsg := nats.NewMsg(correctSubj)
		correctMsg.Header = m.msg.Headers()
		correctMsg.Data = m.msg.Data()
		if _, err := js.PublishMsg(ctx, correctMsg); err != nil {
			return 0, fmt.Errorf("unable to write into %q: %v", correctSubj, err)
		}
		if m.bad {
			// We count as "fixed" only the ones that really had bad encoding.
			fixed++
		}
	}
	stream.DeleteConsumer(ctx, cons.CachedInfo().Name)

	// Now we will delete the original stream
	streamName := "OBJ_" + storeName
	if err := js.DeleteStream(ctx, streamName); err != nil {
		return 0, fmt.Errorf("unable to delete original stream %q: %v", streamName, err)
	}
	metaPrefix := fmt.Sprintf("$O.%s.M.", storeName)
	// Recreate the original stream
	cfg.Name = streamName
	cfg.Subjects = []string{chunkPrefix + ">", metaPrefix + ">"}
	if _, err := js.CreateStream(ctx, cfg); err != nil {
		return 0, fmt.Errorf("unable to recreate stream %q: %v", streamName, err)
	}
	stream = nil
	// Get a stream reference for our temporary stream
	tmpStream, err := js.Stream(ctx, tmpStreamName)
	if err != nil {
		return 0, fmt.Errorf("unable to get reference for stream %q: %v", streamName, err)
	}
	// Copy things over
	cons, err = tmpStream.OrderedConsumer(ctx, jetstream.OrderedConsumerConfig{
		FilterSubjects: []string{">"},
	})
	if err != nil {
		return 0, fmt.Errorf("unable to create consumer: %v", err)
	}
	defer tmpStream.DeleteConsumer(ctx, cons.CachedInfo().Name)

	for range cons.CachedInfo().NumPending {
		msg, err := cons.Next()
		if err != nil {
			return 0, fmt.Errorf("unable to get next chunk: %v", err)
		}
		var subject string
		// Start with chunk prefix
		nuid := strings.TrimPrefix(msg.Subject(), tmpChunkPrefix)
		if nuid != msg.Subject() {
			subject = chunkPrefix + nuid
		} else {
			nuid = strings.TrimPrefix(msg.Subject(), tmpMetaPrefix)
			if nuid != msg.Subject() {
				subject = metaPrefix + nuid
			} else {
				return 0, fmt.Errorf("invalid subject %q", msg.Subject())
			}
		}
		tmpMsg := nats.NewMsg(subject)
		tmpMsg.Header = msg.Headers()
		tmpMsg.Data = msg.Data()
		if _, err := js.PublishMsg(ctx, tmpMsg); err != nil {
			return 0, fmt.Errorf("unable to write into %q: %v", tmpMsg.Subject, err)
		}
	}
	// Now seal the stream
	cfg.Sealed = true
	if _, err := js.UpdateStream(ctx, cfg); err != nil {
		return fixed, fmt.Errorf("unable to seal stream %q: %v", streamName, err)
	}
	// Now delete the temporary stream.
	if err := js.DeleteStream(ctx, tmpStreamName); err != nil {
		return fixed, fmt.Errorf("unable to delete temporary stream %q: %v", tmpStreamName, err)
	}
	return fixed, nil
}

type metaRec struct {
	msg jetstream.Msg
	enc string
	bad bool
	seq uint64
}

func collectMetaRecords(ctx context.Context, js jetstream.JetStream, storeName string) (int, []*metaRec, error) {
	streamName := fmt.Sprintf("OBJ_%s", storeName)
	metaSubjPrexix := fmt.Sprintf("$O.%s.M.", storeName)
	metaSubj := metaSubjPrexix + ">"

	cons, err := js.OrderedConsumer(ctx, streamName, jetstream.OrderedConsumerConfig{
		FilterSubjects: []string{metaSubj},
	})
	if err != nil {
		return 0, nil, fmt.Errorf("unable to create subscription on %q: %v", metaSubj, err)
	}
	defer js.DeleteConsumer(ctx, streamName, cons.CachedInfo().Name)
	ci, err := cons.Info(ctx)
	if err != nil {
		return 0, nil, fmt.Errorf("unable to get consumer info for %q: %v", metaSubj, err)
	}

	var metaRecords []*metaRec
	var badOnes int

	for range ci.NumPending {
		msg, err := cons.Next()
		if err != nil {
			return badOnes, nil, fmt.Errorf("unable to get next message: %v", err)
		}
		data := map[string]any{}
		err = json.Unmarshal(msg.Data(), &data)
		if err != nil {
			return badOnes, nil, fmt.Errorf("unable to parse %q: %v", msg.Data(), err)
		}
		namei, ok := data["name"]
		if !ok {
			return badOnes, nil, fmt.Errorf("field %q missing", "name")
		}
		if _, ok := namei.(string); !ok {
			return badOnes, nil, fmt.Errorf("field %q is of wrong type %T", "name", namei)
		}
		name := namei.(string)
		goodEncoding := base64.URLEncoding.EncodeToString([]byte(name))
		encoding := strings.TrimPrefix(msg.Subject(), metaSubjPrexix)
		bad := encoding != goodEncoding

		r := &metaRec{
			msg: msg,
			enc: goodEncoding,
			bad: bad,
		}
		if bad {
			md, err := msg.Metadata()
			if err != nil {
				return badOnes, nil, fmt.Errorf("unable to get message metadata: %v", err)
			}
			r.seq = md.Sequence.Stream
			badOnes++
		}
		metaRecords = append(metaRecords, r)
	}
	return badOnes, metaRecords, nil
}
