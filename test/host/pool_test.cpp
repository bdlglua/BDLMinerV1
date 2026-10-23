// End-to-end check of the mining core against a real Stratum pool, run on a
// PC with the exact same protocol/work/SHA code as the firmware.
//   ./test/host/run.sh pool [host] [port] [btc_address] [seconds] [threads]
#include <netdb.h>
#include <sys/socket.h>
#include <unistd.h>

#include <atomic>
#include <chrono>
#include <cstdio>
#include <mutex>
#include <thread>
#include <vector>
#include <cstdlib>
#include <cstring>
#include <string>

#include "core/sha256.h"
#include "core/stratum.h"
#include "core/work.h"

using Clock = std::chrono::steady_clock;

static int connectTo(const char *host, const char *port) {
    addrinfo hints{}, *res;
    hints.ai_socktype = SOCK_STREAM;
    if (getaddrinfo(host, port, &hints, &res)) return -1;
    int fd = socket(res->ai_family, res->ai_socktype, res->ai_protocol);
    if (fd >= 0 && connect(fd, res->ai_addr, res->ai_addrlen)) {
        close(fd);
        fd = -1;
    }
    freeaddrinfo(res);
    return fd;
}

static void sendLine(int fd, const std::string &l) {
    printf(">> %s", l.c_str());
    (void)!write(fd, l.data(), l.size());
}

int main(int argc, char **argv) {
    const char *host = argc > 1 ? argv[1] : "public-pool.io";
    const char *port = argc > 2 ? argv[2] : "21496";
    const char *addr = argc > 3 ? argv[3] : "1A1zP1eP5QGefi2DMPTfTL5SLmv7DivfNa";
    int seconds = argc > 4 ? atoi(argv[4]) : 600;
    int threads = argc > 5 ? atoi(argv[5]) : (int)std::thread::hardware_concurrency();
    bool stopOnFirst = true;

    int fd = connectTo(host, port);
    if (fd < 0) {
        printf("cannot connect to %s:%s\n", host, port);
        return 2;
    }
    timeval tv{0, 200000};
    setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));

    stratum::Protocol proto;
    proto.setCredentials(std::string(addr) + ".hosttest", "x");
    sendLine(fd, proto.subscribe("BDLMinerV1/hosttest"));

    // Hashing threads, each on its own extranonce2 like the firmware miners.
    std::mutex mu;  // guards proto (job + submit) and the socket writes
    std::atomic<uint32_t> jobSeq{0};
    std::atomic<bool> stop{false};
    std::atomic<uint64_t> hashes{0};
    std::atomic<int> submitted{0};
    uint64_t en2 = 0;
    std::vector<std::thread> pool;
    for (int t = 0; t < threads; t++) {
        pool.emplace_back([&] {
            work::Work w;
            uint32_t seq = 0, nonce = 0;
            bool have = false;
            uint8_t hash[32];
            while (!stop) {
                if (!have || seq != jobSeq) {
                    std::lock_guard<std::mutex> l(mu);
                    have = proto.hasJob();
                    seq = jobSeq;
                    if (have) work::build(proto.job(), en2++, w);
                    nonce = 0;
                }
                if (!have) {
                    std::this_thread::sleep_for(std::chrono::milliseconds(50));
                    continue;
                }
                for (int k = 0; k < 100000; k++, nonce++) {
                    if (!sha::scan(w.mid, nonce, w.quickLimit, hash)) continue;
                    double d = work::hashDifficulty(hash);
                    if (d < w.shareDifficulty) continue;
                    stratum::Share s{};
                    snprintf(s.jobId, sizeof(s.jobId), "%s", w.jobId.c_str());
                    snprintf(s.extranonce2, sizeof(s.extranonce2), "%s", w.extranonce2Hex.c_str());
                    s.ntime = w.ntime;
                    s.nonce = nonce;
                    s.difficulty = d;
                    std::lock_guard<std::mutex> l(mu);
                    printf("   share diff %.3f\n", d);
                    sendLine(fd, proto.submit(s));
                    submitted++;
                }
                hashes += 100000;
            }
        });
    }

    std::string buf;
    int accepted = 0, rejected = 0;
    auto start = Clock::now(), end = start + std::chrono::seconds(seconds), lastLog = start;
    while (Clock::now() < end) {
        char chunk[4096];
        ssize_t n = read(fd, chunk, sizeof(chunk));
        if (n == 0) {
            printf("pool closed the connection\n");
            break;
        }
        for (ssize_t i = 0; i < n; i++) {
            if (chunk[i] != '\n') {
                buf += chunk[i];
                continue;
            }
            std::lock_guard<std::mutex> l(mu);
            printf("<< %.160s%s\n", buf.c_str(), buf.size() > 160 ? "..." : "");
            stratum::Event ev = proto.handleLine(buf.c_str());
            buf.clear();
            if (ev == stratum::Event::Subscribed) {
                sendLine(fd, proto.suggestDifficulty(0.001));
                sendLine(fd, proto.authorize());
            } else if (ev == stratum::Event::NewJob || ev == stratum::Event::Difficulty) {
                jobSeq++;
                if (proto.hasJob())
                    printf("   job %s height %u pool diff %g\n", proto.job().id.c_str(), proto.job().blockHeight(),
                           proto.job().difficulty);
            } else if (ev == stratum::Event::ShareAccepted) {
                accepted++;
                printf("   ACCEPTED\n");
            } else if (ev == stratum::Event::ShareRejected) {
                rejected++;
                printf("   REJECTED: %s\n", proto.lastError().c_str());
            } else if (ev == stratum::Event::AuthFailed) {
                printf("   auth failed: %s\n", proto.lastError().c_str());
                stop = true;
            }
        }
        if (stop) break;
        if (Clock::now() - lastLog > std::chrono::seconds(30)) {
            lastLog = Clock::now();
            double s = std::chrono::duration<double>(lastLog - start).count();
            printf("   %.0fs  %.2f MH/s  submitted %d accepted %d rejected %d\n", s, hashes / s / 1e6,
                   submitted.load(), accepted, rejected);
            fflush(stdout);
        }
        // Stop as soon as one share has been answered when asked to.
        if (accepted + rejected > 0 && stopOnFirst) break;
    }
    stop = true;
    for (auto &t : pool) t.join();
    close(fd);
    printf("\nsubmitted %d, accepted %d, rejected %d\n", submitted.load(), accepted, rejected);
    return accepted > 0 && rejected == 0 ? 0 : 1;
}
