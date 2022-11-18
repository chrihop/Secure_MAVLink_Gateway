#include <arpa/inet.h>
#include <atomic>
#include <cstdio>
#include <cstring>
#include <list>
#include <netinet/in.h>
#include <string>
#include <sys/socket.h>
#include <threads.h>
#include <unistd.h>
using namespace std;

struct adapter_metrics_t
{
    uint64_t bytes_sent;
    uint64_t bytes_received;
    uint64_t packets_sent;
    uint64_t packets_received;
};

class Adapter
{
protected:
    bool                     terminate;
    struct adapter_metrics_t metrics
    {
    };

public:
    Adapter();
    virtual ~Adapter();
    virtual int       connect() = 0;
    virtual int       disconnect();
    virtual bool      is_connected()                            = 0;
    virtual int       do_send(const uint8_t* data, size_t size) = 0;
    virtual int       do_recv(uint8_t* data, size_t size)       = 0;
    virtual string    to_string()                               = 0;
    int               send(const uint8_t* data, size_t size);
    int               recv(uint8_t* data, size_t size);
    adapter_metrics_t get_metrics();
};

Adapter::Adapter() { terminate = false; }

Adapter::~Adapter() = default;

adapter_metrics_t
Adapter::get_metrics()
{
    return metrics;
}

int
Adapter::disconnect()
{
    terminate = true;
    return 0;
}
int
Adapter::send(const uint8_t* data, size_t size)
{
    if (!is_connected())
    {
        return (int)size;
    }

    int rv = do_send(data, size);
    metrics.packets_sent++;
    metrics.bytes_sent += size;
    return rv;
}
int
Adapter::recv(uint8_t* data, size_t size)
{
    while (!is_connected())
    {
        printf("connect to %s ...\n", to_string().c_str());
        connect();
        sleep(1);
    }

    int rv = do_recv(data, size);
    metrics.packets_received++;
    metrics.bytes_received += rv;
    return rv;
}

class TCPAdapter : public Adapter
{
protected:
    char ip[16] {};
    int  port;
    int  fd;

public:
    TCPAdapter(const char* ip, int port);
    ~TCPAdapter() override;

    int    connect() override;
    int    disconnect() override;
    bool   is_connected() override;
    int    do_send(const uint8_t* data, size_t size) override;
    int    do_recv(uint8_t* data, size_t size) override;
    string to_string() override;
};

TCPAdapter::TCPAdapter(const char* ip, int port)
{
    strncpy(this->ip, ip, sizeof(this->ip));
    this->port = port;
    this->fd   = -1;
}

TCPAdapter::~TCPAdapter()
{
    if (this->fd != -1)
    {
        close(this->fd);
    }
}

string
TCPAdapter::to_string()
{
    char buf[32];
    snprintf(buf, sizeof(buf), "%s:%d", ip, port);
    return string { buf };
}

int
TCPAdapter::connect()
{
    struct sockaddr_in servaddr
    {
    };
    int opt = 1;
    if ((this->fd = socket(AF_INET, SOCK_STREAM, 0)) < 0)
    {
        perror("socket creation failed");
        goto failed;
    }

    if (setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt)) < 0)
    {
        perror("Failed to reset socket!");
        goto failed;
    }

    memset(&servaddr, 0, sizeof(servaddr));
    servaddr.sin_family = AF_INET;
    servaddr.sin_port   = htons(port);
    if (inet_pton(AF_INET, ip, &servaddr.sin_addr) <= 0)
    {
        perror("Invalid address/ Address not supported");
        goto failed;
    }
    if (::connect(fd, (struct sockaddr*)&servaddr, sizeof(servaddr)) < 0)
    {
        perror("Connection Failed");
        goto failed;
    }
    return 0;

failed:
    fd = -1;
    return -1;
}

bool
TCPAdapter::is_connected()
{
    return this->fd != -1;
}

int
TCPAdapter::do_send(const uint8_t* data, size_t size)
{

    ssize_t len = ::send(fd, data, size, 0);
    if (len < 0)
    {
        perror("send failed");
        return -1;
    }
    return (int)len;
}

int
TCPAdapter::do_recv(uint8_t* data, size_t size)
{

    ssize_t len = ::recv(fd, data, size, 0);
    if (len < 0)
    {
        perror("recv failed");
        return -1;
    }
    if (len == 0)
    {
        printf("Connection closed\n");
        close(fd);
        fd = -1;
    }
    return (int)len;
}

int
TCPAdapter::disconnect()
{
    Adapter::disconnect();
    if (this->fd != -1)
    {
        close(this->fd);
        this->fd = -1;
    }
    return 0;
}

struct packet_t
{
    size_t  size;
    uint8_t data[1024];
};

struct tunnel_metrics_t
{
    size_t queue_size;
};

class Tunnel
{
protected:
    list<packet_t*> queue;
    mtx_t           lock {};
    cnd_t           queue_not_empty {};
    thrd_t          receiver {}, sender {};
    atomic_int      terminate;
    int             bandwidth;
    int             max_queue_size {};
    int             sender_thread(void* arg);
    int             receiver_thread(void* arg);

public:
    Adapter* source;
    Adapter* sink;
    Tunnel(Adapter* source, Adapter* sink, int flow_rate);
    ~Tunnel();
    int              start();
    void             set_max_queue_size(int size);
    list<thrd_t>     all_threads();
    tunnel_metrics_t get_metrics();
};

Tunnel::Tunnel(Adapter* source, Adapter* sink, int bandwidth)
{
    this->source    = source;
    this->sink      = sink;
    this->bandwidth = bandwidth;
    this->queue.clear();
    this->receiver = {};
    this->sender   = {};
    mtx_init(&lock, mtx_plain);
    cnd_init(&queue_not_empty);
    terminate = false;
}

Tunnel::~Tunnel()
{
    terminate = true;
    cnd_signal(&queue_not_empty);
    if (receiver)
    {
        thrd_join(receiver, nullptr);
    }
    if (sender)
    {
        thrd_join(sender, nullptr);
    }
    mtx_destroy(&lock);
    cnd_destroy(&queue_not_empty);
}

int
Tunnel::sender_thread(void* arg)
{
    struct timespec tstart
    {
    }, tend {};
    size_t packet_size;
    while (!this->terminate)
    {
        clock_gettime(CLOCK_MONOTONIC, &tstart);
        mtx_lock(&lock);
        while (queue.empty())
        {
            cnd_wait(&queue_not_empty, &lock);
        }
        auto packet = queue.front();
        queue.pop_front();
        mtx_unlock(&lock);
        packet_size = packet->size;
        sink->send(packet->data, packet->size);
        delete packet;

        mtx_lock(&lock);
        if (max_queue_size > 0 && queue.size() > max_queue_size)
        {
            printf("Queue overflow: remove %lu packets from the queue\n",
                queue.size());
            for (auto x : queue)
            {
                delete x;
            }
            queue.clear();
        }
        mtx_unlock(&lock);

        clock_gettime(CLOCK_MONOTONIC, &tend);
        if (bandwidth <= 0)
        {
            continue;
        }

        ssize_t elapsed = (tend.tv_sec - tstart.tv_sec) * 1000000
            + (tend.tv_nsec - tstart.tv_nsec) / 1000;
        uint64_t rate_us = packet_size * 1000000 / bandwidth;

        if (elapsed < rate_us)
        {
            usleep(rate_us - elapsed);
        }
    }

    return 0;
}

int
Tunnel::receiver_thread(void* arg)
{
    while (!this->terminate)
    {
        auto* packet = new packet_t;
        int   len    = source->recv(packet->data, sizeof(packet->data));
        if (len > 0)
        {
            packet->size = len;
            mtx_lock(&lock);
            queue.push_back(packet);
            cnd_signal(&queue_not_empty);
            mtx_unlock(&lock);
        }
    }

    return 0;
}

int
Tunnel::start()
{
    if (thrd_create(
            &receiver,
            [](void* arg) -> int
            { return ((Tunnel*)arg)->receiver_thread(arg); },
            this)
        != thrd_success)
    {
        perror("Failed to create receiver thread");
        return -1;
    }
    if (thrd_create(
            &sender,
            [](void* arg) -> int { return ((Tunnel*)arg)->sender_thread(arg); },
            this)
        != thrd_success)
    {
        perror("Failed to create sender thread");
        return -1;
    }

    return 0;
}

list<thrd_t>
Tunnel::all_threads()
{
    return list<thrd_t> { receiver, sender };
}
tunnel_metrics_t
Tunnel::get_metrics()
{
    tunnel_metrics_t metrics {};
    metrics.queue_size = queue.size();
    return metrics;
}
void
Tunnel::set_max_queue_size(int size)
{
    max_queue_size = size;
}

class Bridge
{
protected:
    Adapter *up_adapt, *down_adapt;

public:
    Tunnel *uplink, *downlink;

    Bridge(Adapter* up_adapt, Adapter* down_adapt, int bandwidth);
    virtual ~Bridge();
    virtual int start();
    void        join();
    void        monitor();
};

Bridge::Bridge(Adapter* up_adapt, Adapter* down_adapt, int bandwidth)
{
    this->up_adapt   = up_adapt;
    this->down_adapt = down_adapt;
    uplink           = new Tunnel(up_adapt, down_adapt, bandwidth);
    downlink         = new Tunnel(down_adapt, up_adapt, 0);
}

Bridge::~Bridge()
{
    this->up_adapt->disconnect();
    this->down_adapt->disconnect();
    delete up_adapt;
    delete down_adapt;
    delete uplink;
    delete downlink;
}

int
Bridge::start()
{
    uplink->start();
    downlink->start();
    return 0;
}

void
Bridge::join()
{
    list<thrd_t> threads = uplink->all_threads();
    threads.splice(threads.end(), downlink->all_threads());
    for (auto& thread : threads)
    {
        thrd_join(thread, nullptr);
    }
}

struct bridge_metrics_t
{
    adapter_metrics_t up_adp, down_adp;
    tunnel_metrics_t  up_tun, down_tun;
};

void
Bridge::monitor()
{
    struct bridge_metrics_t metrics
    {
    };
    while (true)
    {
        sleep(1);
        adapter_metrics_t up       = uplink->source->get_metrics();
        adapter_metrics_t down     = downlink->source->get_metrics();
        tunnel_metrics_t  up_tun   = uplink->get_metrics();
        tunnel_metrics_t  down_tun = downlink->get_metrics();

        printf("down %lu/s %luB/s -> %lu/s %luB/s Q %lu | up %lu/s %luB/s <- "
               "%lu/s %luB/s Q %lu\n",
            up.packets_received - metrics.up_adp.packets_received,
            up.bytes_received - metrics.up_adp.bytes_received,
            down.packets_sent - metrics.down_adp.packets_sent,
            down.bytes_sent - metrics.down_adp.bytes_sent,
            up_tun.queue_size,
            up.packets_sent - metrics.up_adp.packets_sent,
            up.bytes_sent - metrics.up_adp.bytes_sent,
            down.packets_received - metrics.down_adp.packets_received,
            down.bytes_received - metrics.down_adp.bytes_received,
            down_tun.queue_size);

        metrics.up_adp   = up;
        metrics.down_adp = down;
        metrics.up_tun   = up_tun;
        metrics.down_tun = down_tun;
    }
}

class TCPBridge : public Bridge
{
public:
    TCPBridge(const char* uplink_ip, int uplink_port, const char* downlink_ip,
        int downlink_port, int bandwidth);
    ~TCPBridge() override;
};

TCPBridge::TCPBridge(const char* uplink_ip, int uplink_port,
    const char* downlink_ip, int downlink_port, int bandwidth)
    : Bridge(new TCPAdapter(uplink_ip, uplink_port),
        new TCPAdapter(downlink_ip, downlink_port), bandwidth)
{
}

TCPBridge::~TCPBridge() { }

int
main(int argc, char* argv[])
{
    Bridge * bridge;
    if (argc != 6)
    {
        printf("Usage: %s <uplink_ip> <uplink_port> <downlink_ip> "
               "<downlink_port>\n",
            argv[0]);

        bridge = new TCPBridge("127.0.0.1", 5762, "127.0.0.1", 20501, 3000);
    }
    else
    {
        bridge = new TCPBridge(argv[1], atoi(argv[2]), argv[3], atoi(argv[4]),
            atoi(argv[5]));
    }

    bridge->uplink->set_max_queue_size(1000000);
    bridge->start();
    bridge->monitor();
    return 0;
}
