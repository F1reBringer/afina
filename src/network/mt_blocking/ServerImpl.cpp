#include "ServerImpl.h"
#include <cassert>
#include <cstring>
#include <iostream>
#include <memory>
#include <stdexcept>


#include <arpa/inet.h>
#include <netdb.h>
#include <netinet/in.h>
#include <pthread.h>
#include <signal.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <unistd.h>


#include <spdlog/logger.h>


#include <afina/Storage.h>
#include <afina/execute/Command.h>
#include <afina/logging/Service.h>


#include "protocol/Parser.h"


namespace Afina {
namespace Network {
namespace MTblocking {

// See Server.h
ServerImpl::ServerImpl(std::shared_ptr<Afina::Storage> ps, std::shared_ptr<Logging::Service> pl) : Server(ps, pl) {}

// See Server.h
ServerImpl::~ServerImpl() 
{
    Stop();
    if (_thread.joinable()) 
    {
        Join();
    }
}

// See Server.h
void ServerImpl::Start(uint16_t port, uint32_t n_accept, uint32_t n_workers) 
{

    NumWorkers = n_workers;

    _logger = pLogging->select("network");
    _logger->info("Start mt_blocking network service");

    sigset_t sig_mask;
    sigemptyset(&sig_mask);
    sigaddset(&sig_mask, SIGPIPE);
    if (pthread_sigmask(SIG_BLOCK, &sig_mask, NULL) != 0) 
    {
        throw std::runtime_error("Unable to mask SIGPIPE");
    }

    struct sockaddr_in server_addr;
    std::memset(&server_addr, 0, sizeof(server_addr));
    server_addr.sin_family = AF_INET;         // IPv4
    server_addr.sin_port = htons(port);       // TCP port number
    server_addr.sin_addr.s_addr = INADDR_ANY; // Bind to any address

    _server_socket = socket(PF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (_server_socket == -1) 
    {
        throw std::runtime_error("Failed to open socket");
    }

    int opts = 1;
    if (setsockopt(_server_socket, SOL_SOCKET, SO_REUSEADDR, &opts, sizeof(opts)) == -1) 
    {
        close(_server_socket);
        throw std::runtime_error("Socket setsockopt() failed");
    }

    if (bind(_server_socket, (struct sockaddr *)&server_addr, sizeof(server_addr)) == -1)
    {
        close(_server_socket);
        throw std::runtime_error("Socket bind() failed");
    }

    if (listen(_server_socket, 5) == -1)
    {
        close(_server_socket);
        throw std::runtime_error("Socket listen() failed");
    }

    running.store(true);
    _thread = std::thread(&ServerImpl::OnRun, this);
}

// See Server.h
void ServerImpl::Stop()
{
    running.store(false);
    shutdown(_server_socket, SHUT_RDWR);
    
    std::unique_lock<std::mutex> _lock(_mutex);
    for (auto socket : _sockets)
    {
        shutdown(socket, SHUT_RD);
    }
}

// See Server.h
void ServerImpl::Join()
{
    assert(_thread.joinable());
    _thread.join();
    
    std::unique_lock<std::mutex> _lock(_mutex);
    while (!_sockets.empty()) {
        _cv.wait(_lock);
    }
      
}


void ServerImpl::MyWorker(int client_socket)
{
    // Here is connection state
    // - parser: parse state of the stream
    // - command_to_execute: last command parsed out of stream
    // - arg_remains: how many bytes to read from stream to get command argument
    // - argument_for_command: buffer stores argument


    std::size_t arg_remains = 0;
    Protocol::Parser parser;
    std::string argument_for_command;
    std::unique_ptr<Execute::Command> command_to_execute;

    try
    {
    	int ReadBytesAtAll = 0;
    	int ReadBytesNow = -1;
        char client_buffer[4096] = "";
        while ((ReadBytesNow = read(client_socket, client_buffer + ReadBytesAtAll, sizeof(client_buffer) - ReadBytesAtAll)) > 0)
        {
            ReadBytesAtAll += ReadBytesNow;
            _logger->debug("Got {} bytes from socket", ReadBytesNow);
            
            while (ReadBytesAtAll > 0)
            {
                _logger->debug("Process {} bytes", ReadBytesNow);
                // There is no command yet
                if (!command_to_execute)
                {
                    std::size_t parsed = 0;
                    if (parser.Parse(client_buffer,ReadBytesAtAll, parsed))
                    {
                        _logger->debug("Found new command: {} in {} bytes", parser.Name(), parsed);
                        command_to_execute = parser.Build(arg_remains);
                        if (arg_remains > 0)
                        {
                            arg_remains += 2;
                        }
                    }

                    if (parsed == 0)
                    {
                        break;
                    }
                    else
                    {
                        std::memmove(client_buffer, client_buffer + parsed, ReadBytesAtAll - parsed);
                        ReadBytesAtAll -= parsed;
                    }
                }

                if (command_to_execute && arg_remains > 0)
                {
                    _logger->debug("Fill argument: {} bytes of {}", ReadBytesAtAll, arg_remains);
                    std::size_t to_read = std::min(arg_remains, std::size_t(ReadBytesAtAll));
                    argument_for_command.append(client_buffer, to_read);

                    std::memmove(client_buffer, client_buffer + to_read, ReadBytesAtAll - to_read);
                    arg_remains -= to_read;
                    ReadBytesAtAll -= to_read;
                }

                if (command_to_execute && arg_remains == 0)
                {
                    _logger->debug("Start command execution");

                    std::string result;
                    if (argument_for_command.size())
                    {
                        argument_for_command.resize(argument_for_command.size() - 2);
                    }
                    command_to_execute->Execute(*pStorage, argument_for_command, result);

                    // Send response
                    result += "\r\n";
                    if (send(client_socket, result.data(), result.size(), 0) <= 0)
                    {
                        throw std::runtime_error("Failed to send response");
                    }

                    // Prepare for the next command
                    command_to_execute.reset();
                    argument_for_command.resize(0);
                    parser.Reset();
                }
                
            }
            
        }

        if (ReadBytesAtAll == 0)
        {
            _logger->debug("Connection closed");
        }
         else 
         {
            throw std::runtime_error(std::string(strerror(errno)));
        }
    }
    catch (std::runtime_error &ex)
    {
        _logger->error("Failed to process connection on descriptor {}: {}", client_socket, ex.what());
    }

    std::lock_guard<std::mutex> _lock(_mutex);
    close(client_socket);
    _sockets.erase(client_socket);

    if (!running.load() && _sockets.empty())
    {
        _cv.notify_all();
    }

}



// See Server.h
void ServerImpl::OnRun()
{

    while (running.load())
    {
        _logger->debug("waiting for connection...");

        // The call to accept() blocks until the incoming connection arrives
        int client_socket;
        struct sockaddr client_addr;
        socklen_t client_addr_len = sizeof(client_addr);
        if ((client_socket = accept(_server_socket, (struct sockaddr *)&client_addr, &client_addr_len)) == -1)
        {
            continue;
        }

        // Got new connection
        if (_logger->should_log(spdlog::level::debug))
        {
            std::string host = "unknown", port = "-1";

            char hbuf[NI_MAXHOST], sbuf[NI_MAXSERV];
            if (getnameinfo(&client_addr, client_addr_len, hbuf, sizeof(hbuf), sbuf, sizeof(sbuf),
                            NI_NUMERICHOST | NI_NUMERICSERV) == 0){
                host = hbuf;
                port = sbuf;
            }
            _logger->debug("Accepted connection on descriptor {} (host={}, port={})\n", client_socket, host, port);
        }

        {
            struct timeval tv;
            tv.tv_sec = 5; // TODO: make it configurable
            tv.tv_usec = 0;
            setsockopt(client_socket, SOL_SOCKET, SO_RCVTIMEO, (const char *)&tv, sizeof tv);
        }
        
        {
            std::lock_guard<std::mutex> _lock(_mutex);
            if (_sockets.size() < NumWorkers && running.load())
            {
                std::thread(&ServerImpl::MyWorker, this, client_socket).detach();
                _sockets.insert(client_socket);
            }
             else 
            {
                close(client_socket);
            }
        }

    }


    // Cleanup on exit...

    close(_server_socket);

    _logger->warn("Network stopped");
}


} // namespace MTblocking
} // namespace Network
} // namespace Afina
