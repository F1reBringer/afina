#ifndef AFINA_NETWORK_MT_NONBLOCKING_CONNECTION_H
#define AFINA_NETWORK_MT_NONBLOCKING_CONNECTION_H

#include <cstring>
#include <atomic>


#include <sys/epoll.h>
#include <afina/execute/Command.h>
#include <protocol/Parser.h>
#include <spdlog/logger.h>


namespace Afina {
namespace Network {
namespace MTnonblock {

class Connection
{
public:
    Connection(int s, std::shared_ptr<Afina::Storage> ps, std::shared_ptr<spdlog::logger> pl)
        : _socket(s)
        , _pStorage(ps)
        , _logger(pl) {
        std::memset(&_event, 0, sizeof(struct epoll_event));
        _event.data.ptr = this;
        std::memset(_client_buffer, 0, 4096);
        
        _is_alive.store(true, std::memory_order_release);
        _data_available.store(false, std::memory_order_release);
    }
    


    inline bool isAlive() const { return _is_alive.load(std::memory_order_relaxed); }
    
    void Start();

protected:
    void OnError();
    void OnClose();
    void DoRead();
    void DoWrite();

private:
    friend class Worker;
    friend class ServerImpl;

    int _socket;
    struct epoll_event _event;
    
    std::atomic<bool> _is_alive;
    std::atomic<bool> _data_available;
    bool _is_eof = false;
    
    std::shared_ptr<Afina::Storage> _pStorage;
    std::shared_ptr<spdlog::logger> _logger;
    
    char _client_buffer[4096] = "";
    std::size_t _read_bytes = 0;
    std::size_t _write_bytes = 0;

    std::size_t _arg_remains = 0;
    Protocol::Parser _parser;
    std::string _argument_for_command;
    std::unique_ptr<Execute::Command> _command_to_execute;

    std::vector<std::string> _output;
    std::size_t _max_output_size = 4096;
    
};

} // namespace MTnonblock
} // namespace Network
} // namespace Afina

#endif // AFINA_NETWORK_MT_NONBLOCKING_CONNECTION_H

