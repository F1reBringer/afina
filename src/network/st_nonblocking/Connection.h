#ifndef AFINA_NETWORK_ST_NONBLOCKING_CONNECTION_H
#define AFINA_NETWORK_ST_NONBLOCKING_CONNECTION_H

#include <cstring>
#include <vector>

#include <sys/epoll.h>
#include <afina/execute/Command.h>
#include <protocol/Parser.h>
#include <spdlog/logger.h>

namespace Afina {
namespace Network {
namespace STnonblock {

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
    }

    inline bool isAlive() const { return _is_alive; }

    void Start();

protected:
    void OnError();
    void OnClose();
    void DoRead();
    void DoWrite();

private:
    friend class ServerImpl;

    int _socket;
    struct epoll_event _event;

    bool _is_alive = true; // Переменная отвечающая за то жив ли клиентский сокет вообще

    std::shared_ptr<Afina::Storage> _pStorage;
    std::shared_ptr<spdlog::logger> _logger; 

    char _client_buffer[4096]; //Клиентский буфер на 4096
    std::size_t _read_bytes = 0;//Для чтения байтиков
    std::size_t _write_bytes = 0; // Для записи байтиков
    // Как во второй домашке
    std::size_t _arg_remains = 0;
    Protocol::Parser _parser;
    std::string _argument_for_command;
    std::unique_ptr<Execute::Command> _command_to_execute;
    
    std::vector<std::string> _output; // Ответы , которые нужно отдать клиенту
    // На лекции было предложено через deque, но мне чёт привычнее через вектор
    std::size_t _max_output_size = 4096; //Больше чем 4096 ответ быть не может, сори
};

} // namespace STnonblock
} // namespace Network
} // namespace Afina

#endif // AFINA_NETWORK_ST_NONBLOCKING_CONNECTION_H
