package main

import (
	"encoding/csv"
	"flag"
	"fmt"
	"log"
	"os"
	"strconv"
)

func main() {
	flag.Parse()

	if len(os.Args) != 3 {
		log.Fatalf("usage: %s <main> <bench>", os.Args[0])
	}

	fmain, err := os.Open(os.Args[1])
	if err != nil {
		log.Fatal(err)
	}
	main, err := csv.NewReader(fmain).ReadAll()
	if err != nil {
		log.Fatal(err)
	}

	fbench, err := os.Open(os.Args[2])
	if err != nil {
		log.Fatal(err)
	}
	bench, err := csv.NewReader(fbench).ReadAll()
	if err != nil {
		log.Fatal(err)
	}

	if len(main) != len(bench) {
		log.Fatalf("length mismatch: %d != %d", len(main), len(bench))
	}

	for i := range main {
		if main[i][0] != bench[i][0] || main[i][1] != bench[i][1] {
			log.Fatalf("key mismatch: %s %s != %s %s", main[i][0], main[i][1], bench[i][0], bench[i][1])
		}

		vmain, _ := strconv.Atoi(main[i][2])
		vbranch, _ := strconv.Atoi(bench[i][2])

		fmt.Printf("%s,%s,%d,%02f%%\n", main[i][0], main[i][1], vbranch-vmain, float64(vbranch-vmain)/float64(vmain)*100)
	}
}
