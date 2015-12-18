// Copyright 2015 Apcera Inc. All rights reserved.


#ifndef EXAMPLES_H_
#define EXAMPLES_H_

#include <nats.h>
#include <stdio.h>
#include <string.h>

#ifdef _WIN32
#define strcasecmp  _stricmp
#define strdup      _strdup
#else
extern char *strdup(const char *s);
#include <strings.h>
#endif

#define STATS_IN        0x1
#define STATS_OUT       0x2
#define STATS_COUNT     0x4
#define STATS_ERRORS    0x8

#define MAX_SERVERS     (10)

bool        async   = true;
const char  *subj   = "foo";
const char  *txt    = "hello";
const char  *name   = "worker";
int64_t     total   = 1000000;

volatile int64_t count   = 0;
volatile int64_t errors  = 0;
int64_t          start   = 0;
volatile int64_t elapsed = 0;
bool             print   = false;

natsOptions      *opts   = NULL;

const char       *certFile = NULL;
const char       *keyFile  = NULL;

static natsStatus
printStats(int mode, natsConnection *conn, natsSubscription *sub,
           natsStatistics *stats, uint64_t count, uint64_t errors)
{
    natsStatus  s = NATS_OK;
    uint64_t    inMsgs, inBytes, outMsgs, outBytes, queued, reconnected;

    s = natsConnection_GetStats(conn, stats);
    if (s == NATS_OK)
        s = natsStatistics_GetCounts(stats, &inMsgs, &inBytes,
                                     &outMsgs, &outBytes, &reconnected);
    if ((s == NATS_OK) && (sub != NULL))
    {
        s = natsSubscription_QueuedMsgs(sub, &queued);

        // Since we use AutoUnsubscribe(), when the max has been reached,
        // the subscription is automatically closed, so this call would
        // return "Invalid Subscription". Ignore this error.
        if (s == NATS_INVALID_SUBSCRIPTION)
        {
            s = NATS_OK;
            queued = 0;
        }
    }

    if (s == NATS_OK)
    {
        if (mode & STATS_IN)
        {
            printf("In Msgs: %9" PRIu64 " - "\
                   "In Bytes: %9" PRIu64 " - ", inMsgs, inBytes);
        }
        if (mode & STATS_OUT)
        {
            printf("Out Msgs: %9" PRIu64 " - "\
                   "Out Bytes: %9" PRIu64 " - ", outMsgs, outBytes);
        }
        if (mode & STATS_COUNT)
        {
            printf("Delivered: %9" PRIu64 " - ", count);
        }
        if (sub != NULL)
        {
            printf("Queued: %5" PRIu64 " - ", queued);
        }
        if (mode & STATS_ERRORS)
        {
            printf("Errors: %9" PRIu64 " - ", errors);
        }
        printf("Reconnected: %3" PRIu64 "\n", reconnected);
    }

    return s;
}

static void
printPerf(const char *txt, int64_t count, int64_t start, int64_t elapsed)
{
    if (elapsed == 0)
        elapsed = nats_Now() - start;

    if (elapsed <= 0)
        printf("\nNot enough messages or too fast to report performance!\n");
    else
        printf("\n%s %" PRId64 " messages in "\
               "%" PRId64 " milliseconds (%d msgs/sec)\n",
               txt, count, elapsed, (int)((count * 1000) / elapsed));
}

static void
printUsageAndExit(const char *progName, const char *usage)
{
    printf("\nUsage: %s [options]\n\nThe options are:\n\n"\
"-h             prints the usage\n" \
"-s             server url(s) (list of comma separated nats urls)\n" \
"-tls           use secure (SSL/TLS) connection\n" \
"-tlscacert     trusted certificates file\n" \
"-tlscert       client certificate (PEM format only)\n" \
"-tlskey        client private key file (PEM format only)\n" \
"-tlsciphers    ciphers suite\n"
"-tlshost       server certificate's expected hostname\n" \
"-subj          subject (default is 'foo')\n" \
                "%s\n",
                progName, usage);

    natsOptions_Destroy(opts);
    nats_Close();

    exit(1);
}

static natsStatus
parseUrls(const char *urls, natsOptions *opts)
{
    char        *serverUrls[MAX_SERVERS];
    int         count     = 0;
    natsStatus  s         = NATS_OK;
    char        *urlsCopy = NULL;
    char        *commaPos = NULL;
    char        *ptr      = NULL;

    urlsCopy = strdup(urls);
    if (urlsCopy == NULL)
        return NATS_NO_MEMORY;

    memset(serverUrls, 0, sizeof(serverUrls));

    ptr = urlsCopy;

    do
    {
        if (count == MAX_SERVERS)
        {
            s = NATS_INSUFFICIENT_BUFFER;
            break;
        }

        serverUrls[count++] = ptr;
        commaPos = strchr(ptr, ',');
        if (commaPos != NULL)
        {
            ptr = (char*)(commaPos + 1);
            *(commaPos) = '\0';
        }
        else
        {
            ptr = NULL;
        }
    } while (ptr != NULL);

    if (s == NATS_OK)
        s = natsOptions_SetServers(opts, (const char**) serverUrls, count);

    free(urlsCopy);

    return s;
}

static natsOptions*
parseArgs(int argc, char **argv, const char *usage)
{
    natsStatus  s       = NATS_OK;
    bool        urlsSet = false;
    int         i;

    if (natsOptions_Create(&opts) != NATS_OK)
        s = NATS_NO_MEMORY;

    for (i=1; (i<argc) && (s == NATS_OK); i++)
    {
        if ((strcasecmp(argv[i], "-h") == 0)
            || (strcasecmp(argv[i], "-help") == 0))
        {
            printUsageAndExit(argv[0], usage);
        }
        else if (strcasecmp(argv[i], "-s") == 0)
        {
            if (i + 1 == argc)
                printUsageAndExit(argv[0], usage);

            s = parseUrls(argv[++i], opts);
            if (s == NATS_OK)
                urlsSet = true;
        }
        else if (strcasecmp(argv[i], "-tls") == 0)
        {
            s = natsOptions_SetSecure(opts, true);
        }
        else if (strcasecmp(argv[i], "-tlscacert") == 0)
        {
            if (i + 1 == argc)
                printUsageAndExit(argv[0], usage);

            s = natsOptions_LoadCATrustedCertificates(opts, argv[++i]);
        }
        else if (strcasecmp(argv[i], "-tlscert") == 0)
        {
            if (i + 1 == argc)
                printUsageAndExit(argv[0], usage);

            certFile = argv[++i];
        }
        else if (strcasecmp(argv[i], "-tlskey") == 0)
        {
            if (i + 1 == argc)
                printUsageAndExit(argv[0], usage);

            keyFile = argv[++i];
        }
        else if (strcasecmp(argv[i], "-tlsciphers") == 0)
        {
            if (i + 1 == argc)
                printUsageAndExit(argv[0], usage);

            s = natsOptions_SetCiphers(opts, argv[++i]);
        }
        else if (strcasecmp(argv[i], "-tlshost") == 0)
        {
            if (i + 1 == argc)
                printUsageAndExit(argv[0], usage);

            s = natsOptions_SetExpectedHostname(opts, argv[++i]);
        }
        else if (strcasecmp(argv[i], "-sync") == 0)
        {
            async = false;
        }
        else if (strcasecmp(argv[i], "-subj") == 0)
        {
            if (i + 1 == argc)
                printUsageAndExit(argv[0], usage);

            subj = argv[++i];
        }
        else if (strcasecmp(argv[i], "-name") == 0)
        {
            if (i + 1 == argc)
                printUsageAndExit(argv[0], usage);

            name = argv[++i];
        }
        else if (strcasecmp(argv[i], "-count") == 0)
        {
            if (i + 1 == argc)
                printUsageAndExit(argv[0], usage);

            total = atol(argv[++i]);
        }
        else if (strcasecmp(argv[i], "-text") == 0)
        {
            if (i + 1 == argc)
                printUsageAndExit(argv[0], usage);

            txt = argv[++i];
        }
        else
        {
            printf("Unknown option: '%s'\n", argv[i]);
            printUsageAndExit(argv[0], usage);
        }
    }

    if ((s == NATS_OK) && ((certFile != NULL) || (keyFile != NULL)))
        s = natsOptions_LoadCertificatesChain(opts, certFile, keyFile);

    if ((s == NATS_OK) && !urlsSet)
        s = parseUrls(NATS_DEFAULT_URL, opts);

    if (s != NATS_OK)
    {
        printf("Error parsing arguments: %d - %s\n",
               s, natsStatus_GetText(s));

        nats_PrintLastErrorStack(stderr);

        natsOptions_Destroy(opts);
        nats_Close();
        exit(1);
    }

    return opts;
}

#endif /* EXAMPLES_H_ */
