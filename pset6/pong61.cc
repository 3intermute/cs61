#include "serverinfo.h"
#include "pongboard.hh"
#include <unistd.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <netdb.h>
#include <cstdlib>
#include <cstring>
#include <strings.h>
#include <cstdio>
#include <cerrno>
#include <csignal>
#include <cassert>
#include <atomic>
#include <thread>
#include <vector>
#include <algorithm>
#include <chrono>

#define BACKOFF_K 10000

static const char* pong_host = PONG_HOST;
static int pong_port = PONG_PORT;
static const char* pong_user = PONG_USER;
static struct addrinfo* pong_addr;

// TIME HELPERS
double start_time = 0;

// tstamp()
//    Return the current absolute time as a real number of seconds.
double tstamp() {
    struct timespec now;
    clock_gettime(CLOCK_REALTIME, &now);
    return now.tv_sec + now.tv_nsec * 1e-9;
}

// elapsed()
//    Return the number of seconds that have elapsed since `start_time`.
double elapsed() {
    return tstamp() - start_time;
}


// HTTP CONNECTION MANAGEMENT

// `http_connection::cstate` values
typedef enum http_connection_state {
    cstate_idle = 0,          // Waiting to send request
    cstate_waiting = 1,       // Sent request, waiting to receive response
    cstate_headers = 2,       // Receiving headers
    cstate_body = 3,          // Receiving body
    cstate_closed = -1,       // Body complete, connection closed
    cstate_broken = -2        // Parse error
} http_connection_state;


// http_connection
//    This object represents an open HTTP connection to a server.
struct http_connection {
    int fd_;                  // Socket file descriptor

    http_connection_state cstate_ = cstate_idle; // Connection state (see above)
    int status_code_;         // Response status code (e.g., 200, 402)
    size_t content_length_;   // Content-Length value
    bool has_content_length_; // true iff Content-Length was provided
    bool eof_ = false;        // true iff connection EOF has been reached

    char *buf_ = nullptr;     // Response buffer
    size_t buf_sz;            // reponse buffer size
    size_t len_;              // Length of response buffer


    http_connection(int fd) {
        assert(fd >= 0);
        this->fd_ = fd;
        this->buf_ = (char *) malloc(BUFSIZ);
        this->buf_sz = BUFSIZ;
    }
    ~http_connection() {
        close(this->fd_);
        free(this->buf_);
    }

    // disallow copying and assignment
    http_connection(const http_connection&) = delete;
    http_connection& operator=(const http_connection&) = delete;


    void send_request(const char* uri);
    void receive_response_headers();
    void receive_response_body();
    char* truncate_response();
    bool process_response_headers();
    bool check_response_body();
};

struct proxy {
    int port;
    long time; // time it took to query in ms
};


// http_connect(ai)
//    Open a new connection to the server described by `ai`. Returns a new
//    `http_connection` object for that server connection. Exits with an
//    error message if the connection fails.
http_connection* http_connect(const struct addrinfo* ai) {
    // connect to the server
    int fd = socket(AF_INET, SOCK_STREAM, 0);
    if (fd < 0) {
        perror("socket");
        exit(1);
    }

    int yes = 1;
    (void) setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &yes, sizeof(int));

    int r = connect(fd, ai->ai_addr, ai->ai_addrlen);
    if (r < 0) {
        perror("connect");
        exit(1);
    }

    // construct an http_connection object for this connection
    return new http_connection(fd);
}


// http_close(conn)
//    Close the HTTP connection `conn` and free its resources.
void http_close(http_connection* conn) {
    delete conn;
}


// http_connection::send_request(conn, uri)
//    Send an HTTP POST request for `uri` to this connection.
//    Exit on error.
void http_connection::send_request(const char* uri) {
    assert(this->cstate_ == cstate_idle);

    // prepare and write the request
    char reqbuf[BUFSIZ];
    size_t reqsz = snprintf(reqbuf, sizeof(reqbuf),
                            "POST /%s/%s HTTP/1.0\r\n"
                            "Host: %s\r\n"
                            "Connection: keep-alive\r\n"
                            "\r\n",
                            pong_user, uri, pong_host);
    assert(reqsz < sizeof(reqbuf));

    size_t pos = 0;
    while (pos < reqsz) {
        ssize_t nw = write(this->fd_, &reqbuf[pos], reqsz - pos);
        if (nw == 0) {
            break;
        } else if (nw == -1 && errno != EINTR && errno != EAGAIN) {
            perror("write");
            exit(1);
        } else if (nw != -1) {
            pos += nw;
        }
    }

    if (pos != reqsz) {
        fprintf(stderr, "%.3f sec: connection closed prematurely\n",
                elapsed());
        exit(1);
    }

    // clear response information
    this->cstate_ = cstate_waiting;
    this->status_code_ = -1;
    this->content_length_ = 0;
    this->has_content_length_ = false;
    this->len_ = 0;
}


// http_connection::receive_response_headers()
//    Read the server's response headers and set `status_code_`
//    to the server's status code. If the connection terminates
//    prematurely, `status_code_` is set to -1.
void http_connection::receive_response_headers() {
    assert(this->cstate_ != cstate_idle);
    if (this->cstate_ < 0) {
        return;
    }
    this->buf_[0] = 0;

    // read & parse data until `http_process_response_headers`
    // tells us to stop
    while (this->process_response_headers()) {
        // part 5: ensure this->buf_sz is always > this->len_ + bufsiz
        // could prob be faster with some sorta exponential size increase, less reallocs
        if (this->len_ + BUFSIZ >= this->buf_sz)
        {
            this->buf_ = (char *) realloc(this->buf_, this->buf_sz += BUFSIZ);
        }

        ssize_t nr = read(this->fd_, &this->buf_[this->len_], BUFSIZ);
        if (nr == 0) {
            this->eof_ = true;
            this->buf_[this->len_] = 0;  // null-terminate
        } else if (nr == -1 && errno != EINTR && errno != EAGAIN) {
            perror("read");
            exit(1);
        } else if (nr != -1) {
            this->len_ += nr;
        }
    }

    // Status codes >= 500 mean we are overloading the server
    // and should exit.
    if (this->status_code_ >= 500) {
        fprintf(stderr, "%.3f sec: exiting because of "
                "server status %d (%s)\n", elapsed(),
                this->status_code_, this->truncate_response());
        exit(1);
    }
}


// http_connection::receive_response_body()
//    Read the server's response body. On return, `buf_` holds the
//    response body, which is `len_` bytes long and has been
//    null-terminated.
void http_connection::receive_response_body() {
    assert(this->cstate_ < 0 || this->cstate_ == cstate_body);
    if (this->cstate_ < 0) {
        return;
    }
    // NB: buf_ might contain some body data already!

    // read response body (check_response_body tells us when to stop)
    while (this->check_response_body()) {
        // part 5 here too
        if (this->len_ + BUFSIZ >= this->buf_sz)
        {
            this->buf_ = (char *) realloc(this->buf_, this->buf_sz += BUFSIZ);
        }

        ssize_t nr = read(this->fd_, &this->buf_[this->len_], BUFSIZ);
        if (nr == 0) {
            this->eof_ = true;
            this->buf_[this->len_] = 0;  // null-terminate
        } else if (nr == -1 && errno != EINTR && errno != EAGAIN) {
            perror("read");
            exit(1);
        } else if (nr != -1) {
            this->len_ += nr;
        }
    }
}


// http_connection::truncate_response()
//    Truncate the response text to a manageable length and return
//    that truncated text. Useful for error messages.
char* http_connection::truncate_response() {
    char* eol = strchr(this->buf_, '\n');
    if (eol) {
        *eol = 0;
    }
    if (strnlen(this->buf_, 100) >= 100) {
        this->buf_[100] = 0;
    }
    return this->buf_;
}


// MAIN PROGRAM
// main thread synch objs
std::mutex mutex_;
std::condition_variable_any block_;

// connection table, and table mutex
std::mutex table_mutex;
std::vector<http_connection *> conns;

// pong_thread(x, y)
//    Connect to the server at the position `x, y`.
void pong_thread(int x, int y) {
    char url[256];
    snprintf(url, sizeof(url), "move?x=%d&y=%d&style=on", x, y);

    http_connection* conn = nullptr;
    int fails = 0;

    // fix for 3c: https://piazza.com/class/kdykp5f5e556r2?cid=1528
    for (auto it = conns.begin(); it != conns.end(); ++it)
    {
        if ((*it)->cstate_ == cstate_idle)
        {
            conn = *it;
        }
    }

    while (true)
    {
        if (conn == nullptr)
        {
            conn = http_connect(pong_addr);
            {
                std::unique_lock<std::mutex> table_lock(table_mutex);
                conns.push_back(conn);
            }
        }

        conn->send_request(url);
        conn->receive_response_headers();
        double result = strtod(conn->buf_, nullptr);
        if (result > 0)
        {
            std::unique_lock<std::mutex> table_lock(mutex_);
            std::this_thread::sleep_for(std::chrono::milliseconds((int) result));
        }

        if (conn->status_code_ != 200) {
            ++fails;
            {
                std::unique_lock<std::mutex> table_lock(table_mutex);
                conns.erase(std::remove(conns.begin(), conns.end(), conn), conns.end());
            }
            http_close(conn);
            conn = nullptr;

            usleep((1 << fails) * BACKOFF_K);
        }
        else
        {
            block_.notify_all();
            break;
        }
    }

    conn->receive_response_body();
    double result = strtod(conn->buf_, nullptr);
    if (result < 0) {
        fprintf(stderr, "%.3f sec: server returned error: %s\n",
                elapsed(), conn->truncate_response());
        exit(1);
    }
    else if (result > 0)
    {
        std::unique_lock<std::mutex> table_lock(mutex_);
        std::this_thread::sleep_for(std::chrono::milliseconds((int) result));
    }

    if (conn != nullptr)
    {
        free(conn->buf_);
        conn->buf_ = (char *) malloc(BUFSIZ);
        conn->buf_sz = BUFSIZ;
    }
}


// lookup_tcp_server(host, port)
//    Look up the network address of a TCP server and return its `addrinfo*`.
//    Exits on failure. To avoid memory leaks, call `freeaddrinfo(ret)` on
//    the return value when you are done with it.
static struct addrinfo* lookup_tcp_server(const char* host, int port) {
    struct addrinfo hints, *result;
    memset(&hints, 0, sizeof(hints));
    hints.ai_family = AF_INET;
    hints.ai_socktype = SOCK_STREAM;
    hints.ai_flags = AI_NUMERICSERV;
    std::string portstr = std::to_string(port);
    int r = getaddrinfo(host, portstr.c_str(), &hints, &result);
    if (r != 0) {
        fprintf(stderr, "problem looking up %s:%d: %s\n",
                host, port, gai_strerror(r));
        exit(1);
    }
    return result;
}


// usage()
//    Explain how pong61 should be run.
static void usage() {
    fprintf(stderr, "Usage: ./pong61 [-h HOST] [-p PORT] [-l LATENCY] [-x] [USER]\n");
    exit(1);
}


proxy current_best = {-1, -1};
std::atomic<int> done_count = 0;

void check_proxy(int proxy_num)
{
    int current_port = PROXY_START_PORT + proxy_num;
    http_connection* conn = http_connect(lookup_tcp_server(pong_host, current_port));
    
    conn->send_request("query?x=0&y=0");

    auto start = std::chrono::high_resolution_clock::now();
    conn->receive_response_headers();
    conn->receive_response_body();
    auto finish = std::chrono::high_resolution_clock::now();

    http_close(conn);

    auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(finish - start);

    {
        std::unique_lock<std::mutex> proxy_lock(mutex_);
        if (elapsed.count() < current_best.time || current_best.time == -1)
        {
            current_best = (proxy) {current_port, elapsed.count()};
        }

        ++done_count;
        if (done_count == PROXY_COUNT)
        {
            block_.notify_all();
        }
    }
}


// main(argc, argv)
//    The main loop.
int main(int argc, char** argv) {
    // parse arguments
    int ch;
    bool nocheck = false, fast = false, proxy = false,
        has_host = false, has_port = false;
    unsigned long latency = 0;
    while ((ch = getopt(argc, argv, "nfxh:p:u:l:")) != -1) {
        if (ch == 'h') {
            pong_host = optarg;
            has_host = true;
        } else if (ch == 'p') {
            pong_port = strtol(optarg, nullptr, 0);
            if (pong_port <= 0 || pong_port > 65535) {
                usage();
            }
            has_port = true;
        } else if (ch == 'u') {
            pong_user = optarg;
        } else if (ch == 'l') {
            char* last;
            latency = strtoul(optarg, &last, 0);
            if (*last != '\0' || last == optarg) {
                usage();
            }
        } else if (ch == 'n') {
            nocheck = true;
        } else if (ch == 'f') {
            fast = true;
        } else if (ch == 'x') {
            proxy = true;
        } else {
            usage();
        }
    }
    if (optind == argc - 1) {
        pong_user = argv[optind];
    } else if (optind != argc) {
        usage();
    }
    if (strcmp(pong_user, "proxy-minlan-test") == 0) {
        fprintf(stderr, "You must pick your own PONG_USER first!\n");
        fprintf(stderr, "Edit `serverinfo.h` to pick a PONG_USER.\n");
    }

    // Given `-x`, default to PROXY_HOST:PROXY_START_PORT.
    if (!has_host && proxy) {
        pong_host = PROXY_HOST;
    }
    if (!has_port && proxy) {
        for (int i = 0; i < PROXY_COUNT; ++i)
        {
            std::thread proxy_th;
            try {
                proxy_th = std::thread(check_proxy, i);
            } catch (std::system_error& err) {
                fprintf(stderr, "%.3f sec: cannot create thread: %s\n",
                        elapsed(), err.what());
                exit(1);
            }
            proxy_th.detach();
        }

        // wait for all threads to finish
        block_.wait(mutex_);

        pong_port = current_best.port;
    }

    // look up network address of pong server
    pong_addr = lookup_tcp_server(pong_host, pong_port);

    // reset pong board and get its dimensions
    int width, height, delay = 100000;
    {
        http_connection* conn = http_connect(pong_addr);
        if (!nocheck && !fast && !latency) {
            conn->send_request("reset");
        } else {
            char buf[256];
            sprintf(buf, "reset?nocheck=%d&fast=%d&latency=%lu",
                    nocheck, fast, latency);
            conn->send_request(buf);
        }
        conn->receive_response_headers();
        conn->receive_response_body();
        int nchars;
        if (conn->status_code_ != 200
            || sscanf(conn->buf_, "%d %d %n", &width, &height, &nchars) < 2
            || width <= 0 || height <= 0) {
            fprintf(stderr, "bad response to \"reset\" RPC: %d %s\n",
                    conn->status_code_, conn->truncate_response());
            exit(1);
        }
        (void) sscanf(conn->buf_ + nchars, "%d", &delay);
        http_close(conn);
    }
    // measure future times relative to this moment
    start_time = tstamp();

    // print display URL
    printf("Display: http://%s:%d/%s/%s\n",
           pong_host, pong_port, pong_user,
           nocheck ? " (NOCHECK mode)" : "");

    // play game
    pong_board board(width, height);
    pong_ball ball(board, 0, 0, 1, 1);

    while (1) {
        // create a new thread to handle the next position
        // (wrapped in a try-catch block to catch exceptions)
        std::thread th;
        try {
            th = std::thread(pong_thread, ball.x_, ball.y_);
        } catch (std::system_error& err) {
            fprintf(stderr, "%.3f sec: cannot create thread: %s\n",
                    elapsed(), err.what());
            exit(1);
        }
        th.detach();

        block_.wait(mutex_);

        // update position
        while (ball.move() <= 0) {
        }

        // wait 0.1sec
        usleep(delay);
    }
}


// HTTP PARSING

// http_connection::process_response_headers()
//    Parse the response represented by `conn->buf`. Returns true
//    if more header data remains to be read, false if all headers
//    have been consumed.
bool http_connection::process_response_headers() {
    size_t i = 0;
    while ((this->cstate_ == cstate_waiting || this->cstate_ == cstate_headers)
           && i + 2 <= this->len_) {
        if (this->buf_[i] == '\r' && this->buf_[i + 1] == '\n') {
            this->buf_[i] = 0;
            if (this->cstate_ == cstate_waiting) {
                int minor;
                if (sscanf(this->buf_, "HTTP/1.%d %d",
                           &minor, &this->status_code_) == 2) {
                    this->cstate_ = cstate_headers;
                } else {
                    this->cstate_ = cstate_broken;
                }
            } else if (i == 0) {
                this->cstate_ = cstate_body;
            } else if (strncasecmp(this->buf_, "Content-Length: ", 16) == 0) {
                this->content_length_ = strtoul(this->buf_ + 16, nullptr, 0);
                this->has_content_length_ = true;
            }
            // We just consumed a header line (i+2) chars long.
            // Move the rest of the data down, including terminating null.
            memmove(this->buf_, this->buf_ + i + 2, this->len_ - (i + 2) + 1);
            this->len_ -= i + 2;
            i = 0;
        } else {
            ++i;
        }
    }

    if (this->eof_) {
        this->cstate_ = cstate_broken;
    }
    return this->cstate_ == cstate_waiting || this->cstate_ == cstate_headers;
}


// http_connection::check_response_body()
//    Returns true if more response data should be read into `buf_`,
//    false otherwise (the connection is broken or the response is complete).
bool http_connection::check_response_body() {
    if (this->cstate_ == cstate_body
        && (this->has_content_length_ || this->eof_)
        && this->len_ >= this->content_length_) {
        this->cstate_ = cstate_idle;
    }
    if (this->eof_) {
        if (this->cstate_ == cstate_idle) {
            this->cstate_ = cstate_closed;
        } else if (this->cstate_ != cstate_closed) {
            this->cstate_ = cstate_broken;
        }
    }
    return this->cstate_ == cstate_body;
}
