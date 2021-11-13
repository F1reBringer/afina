#include "Connection.h"

#include <iostream>

#include <sys/uio.h>

namespace Afina {
namespace Network {
namespace MTnonblock {

// See Connection.h
void Connection::Start()
{
    _logger->debug("Start {} socket", _socket);
    _read_bytes = 0;
    _write_bytes = 0;
    _event.data.fd = _socket; 
    _event.data.ptr = this;
    _event.events = EPOLLIN | EPOLLRDHUP | EPOLLERR | EPOLLET;
    _output.clear();
}

// See Connection.h
void Connection::OnError()
{
    _logger->debug("OnError {} socket", _socket);
    _is_alive.store(false, std::memory_order_relaxed);
}

// See Connection.h
void Connection::OnClose()
{
    _logger->debug("OnClose {} socket", _socket);
    _is_alive.store(false, std::memory_order_relaxed);
}

// See Connection.h
void Connection::DoRead()
{

    _logger->debug("DoRead {} socket", _socket);
    
    std::atomic_thread_fence(std::memory_order_acquire);
    
    try
    {
    
    
        int readed_bytes = -1;
        while((readed_bytes = read(_socket, _client_buffer + _read_bytes, sizeof(_client_buffer) - _read_bytes)) > 0)
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


                    result += "\r\n";
                    _output.push_back(result);

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

        if (_read_bytes == 0)
        {
            _logger->debug("Connection closed");
            _data_available.store(true, std::memory_order_relaxed);
            std::atomic_thread_fence(std::memory_order_release);
        } 
        else if(errno != EAGAIN && errno != EINTR && errno != EWOULDBLOCK)
        {
            throw std::runtime_error(std::string(strerror(errno)));
        }
    } 
    catch (std::runtime_error &ex)
    {
        _logger->error("Failed to process connection on descriptor {}: {}", _socket, ex.what());
        
        _output.push_back("ERROR\r\n");
        if (!(_event.events & EPOLLOUT))
        {
            _event.events |= EPOLLOUT;
        }
        _event.events &= ~EPOLLIN;
        
        _data_available.store(true, std::memory_order_relaxed);
        std::atomic_thread_fence(std::memory_order_release);
    }
}

// See Connection.h
void Connection::DoWrite()
{
   try
   {
    _logger->debug("DoWrite {} socket", _socket);
    
    
    std::atomic_thread_fence(std::memory_order_acquire);
    if (!_data_available.load(std::memory_order_relaxed))
    {
        return;
    }
    
    
    struct iovec data[_output.size()] = {};
    size_t i = 0;
    for (i = 0; i < _output.size(); ++i)
    {
        data[i].iov_base = &(_output[i][0]);
        data[i].iov_len = _output[i].size();
    }

    data[0].iov_base = static_cast<char *>(data[0].iov_base) + _write_bytes;
    data[0].iov_len -= _write_bytes;

    int written_bytes = writev(_socket, data, i);
	
    
    if( written_bytes < 0 && errno != EINTR && errno!= EAGAIN)
    {
    	
    	throw std::runtime_error(std::string(strerror(errno)));
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
        _is_alive.store(false, std::memory_order_relaxed);
    }

    if (_output.size() <= 0.9 * _max_output_size)
    {
        _event.events |= EPOLLIN;
    }
    
    }
    catch (std::runtime_error &ex)
    {
    	_logger->error("Failed to write connection on descriptor {}: {}", _socket, ex.what());
    	_is_alive.store(false, std::memory_order_release);
    }
    
}

} // namespace MTnonblock
} // namespace Network
} // namespace Afina
