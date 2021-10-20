#include "Connection.h"

#include <iostream>

#include <sys/uio.h>

namespace Afina {
namespace Network {
namespace STnonblock {

// See Connection.h
void Connection::Start()
{
    _logger->debug("Start {} socket", _socket);
    _is_alive = true; // Клиент у нас есть
    _read_bytes = 0; // Байтики не прочитали
    _write_bytes = 0; // Байтики не записали
    _event.data.fd = _socket; 
    _event.data.ptr = this;
    _event.events = EPOLLIN | EPOLLRDHUP | EPOLLERR; // Что может быть на старте? Чтение, закрытие соединения или ошибка
    _output.clear(); // прошлые ответы чистим,мало ли
}

// See Connection.h
void Connection::OnError()
{
    _logger->debug("OnError {} socket", _socket); // Ошибочка
    _is_alive = false; // Отключаем клиента
}

// See Connection.h
void Connection::OnClose()
{
    _logger->debug("OnClose {} socket", _socket); // Соединение закрыто
    _is_alive = false;
}

// See Connection.h
void Connection::DoRead()
{
    _logger->debug("DoRead {} socket", _socket); // Начинаем чтение
    // Всё как во второй домашке
    try {
        int readed_bytes = -1;
        while ((readed_bytes = read(_socket, _client_buffer + _read_bytes, sizeof(_client_buffer) - _read_bytes)) > 0)
        {
            _read_bytes += readed_bytes;
            _logger->debug("Got {} bytes from socket", readed_bytes);


            while (_read_bytes > 0) {
                _logger->debug("Process {} bytes", readed_bytes);
                if (!_command_to_execute)
                {
                    std::size_t parsed = 0;
                    if (_parser.Parse(_client_buffer, _read_bytes, parsed))
                    {
                        _logger->debug("Found new command: {} in {} bytes", _parser.Name(), parsed);
                        _command_to_execute = _parser.Build(_arg_remains);
                        if (_arg_remains > 0)
                        {
                            _arg_remains += 2;
                        }
                    }
                    if (parsed == 0)
                    {
                        break;
                    }
                    else
                    {
                        std::memmove(_client_buffer, _client_buffer + parsed, _read_bytes - parsed);
                        _read_bytes -= parsed;
                    }
                }

                // There is command, but we still wait for argument to arrive...
                if (_command_to_execute && _arg_remains > 0)
                {
                    _logger->debug("Fill argument: {} bytes of {}", _read_bytes, _arg_remains);
                    // There is some parsed command, and now we are reading argument
                    std::size_t to_read = std::min(_arg_remains, std::size_t(_read_bytes));
                    _argument_for_command.append(_client_buffer, to_read);

                    std::memmove(_client_buffer, _client_buffer + to_read, _read_bytes - to_read);
                    _arg_remains -= to_read;
                    _read_bytes -= to_read;
                }

                // Thre is command & argument - RUN!
                if (_command_to_execute && _arg_remains == 0)
                {
                    _logger->debug("Start command execution");

                    std::string result;
                    if (_argument_for_command.size())
                    {
                        _argument_for_command.resize(_argument_for_command.size() - 2);
                    }
                    _command_to_execute->Execute(*_pStorage, _argument_for_command, result);






                    // Надо сохранить ответик
                    result += "\r\n";
                    _output.push_back(result); // Вкидываем в вектор ответов

                    if (!(_event.events & EPOLLOUT))
                    {
                        _event.events |= EPOLLOUT;
                    }

                    if (_output.size() > _max_output_size)
                    {
                        _event.events &= ~EPOLLIN;
                    }

                    // Prepare for the next command
                    _command_to_execute.reset();
                    _argument_for_command.resize(0);
                    _parser.Reset();
                }
            } // while (_read_bytes)
        }

        if (_read_bytes == 0) // Прочитали всё?
        {
            _logger->debug("Connection closed"); //Закрылись
        } else
        {
            throw std::runtime_error(std::string(strerror(errno)));
        }
    } 
    catch (std::runtime_error &ex)
    {
        _logger->error("Failed to process connection on descriptor {}: {}", _socket, ex.what());
        
        _output.push_back("ERROR\r\n"); // Сори,ошибка
        if (!(_event.events & EPOLLOUT))
        {
            _event.events |= EPOLLOUT;
        }
        _event.events &= ~EPOLLIN;
    }
}

// See Connection.h
void Connection::DoWrite()
{
    _logger->debug("DoWrite {} socket", _socket); // Запись

    struct iovec data[_output.size()];
    size_t i = 0;
    for (i = 0; i < _output.size(); ++i)
    {
        data[i].iov_base = &(_output[i][0]);
        data[i].iov_len = _output[i].size();
    }

    data[0].iov_base = static_cast<char *>(data[0].iov_base) + _write_bytes;
    data[0].iov_len -= _write_bytes;

    int written_bytes = writev(_socket, data, i);

    if (written_bytes <= 0)
    {
        _is_alive = false;
        throw std::runtime_error("Failed to send response");
    }

    i = 0;
    for (auto command : _output)
    {
        if (written_bytes >= command.size())
        {
            ++i;
            written_bytes -= command.size();
        } 
        else 
        {
            break;
        }
    }

    _output.erase(_output.begin(), _output.begin() + i);
    _write_bytes = written_bytes;

    if (_output.empty())
    {
        _event.events &= ~EPOLLOUT;
    }

    if (_output.size() <= _max_output_size)
    {
        _event.events |= EPOLLIN;
    }
}

} // namespace STnonblock
} // namespace Network
} // namespace Afina
