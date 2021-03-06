/*
MIT License

Copyright (c) 2016 Abdel Sako

Permission is hereby granted, free of charge, to any person obtaining a copy
of this software and associated documentation files (the "Software"), to deal
in the Software without restriction, including without limitation the rights
to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
copies of the Software, and to permit persons to whom the Software is
furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in all
copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
SOFTWARE.
*/

#include "TCPserver.hpp"
#include "SocketException.hpp"

/*  */
uint16_t net::TCPserver::serverInstances = 0;
std::condition_variable net::TCPserver::m_intSigCond;
bool net::TCPserver::m_shutdownTCPservers = false;

/* LISTEN FOR INCOMING CONNECTION */
int net::TCPserver::listen(uint16_t maxHost) const throw (net::SocketException)
{
     if(!isValid()) {
          errno = EBADF;
          throw net::SocketException("listen()", errno);
     }

     if(::listen(TCPsocket::m_sockfd, maxHost) == -1) {
          throw net::SocketException("listen()", errno);
     }
     else return 0;
}

/* ACCEPT INCOMING CONNECTION */
net::TCPpeer* net::TCPserver::accept(void) const
throw (net::SocketException)
{
     if(!isValid()) {
          errno = EBADF;
          throw net::SocketException("net::TCPserver::accept()", errno);
     }

     net::SOCKET remoteSockfd;
     socklen_t peerAddrSize;
     void *addr;
     size_t addrsize = this->addrFamily == AF_INET ?
                        INET_ADDRSTRLEN : INET6_ADDRSTRLEN;
     char addrstr[addrsize];
     uint16_t port;
     struct net::PeerInfo peerInfo;
     std::memset(&peerInfo, 0, sizeof peerInfo);

    switch (addrFamily) {
        case AF_INET:
            struct sockaddr_in peerAddr;
            std::memset(&peerAddr, 0, sizeof peerAddr);
            peerAddrSize = sizeof peerAddr;

            remoteSockfd = ::accept(m_sockfd, (struct sockaddr *)&peerAddr,
                                    &peerAddrSize);
            addr = (void*)(&peerAddr.sin_addr);
            port = ::ntohs(peerAddr.sin_port);
            ::inet_ntop(AF_INET, addr, addrstr, addrsize);
            break;

        case AF_INET6:
            struct sockaddr_in6 peerAddr6;
            std::memset(&peerAddr6, 0, sizeof peerAddr6);
            peerAddrSize = sizeof peerAddr6;

            remoteSockfd = ::accept(m_sockfd, (struct sockaddr *)&peerAddr6,
                                    &peerAddrSize);
            addr = (void*)(&peerAddr6.sin6_addr);
            port = ::ntohs(peerAddr.sin_port);
            ::inet_ntop(AF_INET6, addr, addrstr, addrsize);
            break;
        default:
            errno = EAFNOSUPPORT;
            throw net::SocketException("net::TCPserver::accept()", errno);
            break;
    }

    if(remoteSockfd == -1)
        throw net::SocketException("net::TCPserver::accept()", errno);
    else {
 //       net::TCPpeer *peer = new net::TCPpeer(remoteSockfd);
        peerInfo.addr = addrstr;
        peerInfo.port = port;
        peerInfo.af = this->addrFamily;
        net::TCPpeer *peer = new net::TCPpeer(remoteSockfd, peerInfo);
        return peer;
    }
}

int net::TCPserver::startServer(size_t threadNum)
{
    if(threadNum == 0) return -1;

	/* signal handler */
	std::signal(SIGINT, TCPserver::signalHandler);
	std::signal(SIGPIPE, TCPserver::signalHandler);

	/* Listen */
	net::TCPserver::listen(threadNum);

	std::deque<std::thread> acceptThreads;
	std::cout << "[+] Starting TCPserver #" << serverInstances << " with "
        << threadNum << " threads\n";

	std::shared_ptr<net::TCPserver> ptr;
	ptr.reset(this);

	/* Store threads in a vector */
	for( ; threadNum > 0; threadNum--)
		acceptThreads.push_back(std::thread(net::TCPserverThreadCore, ptr));

	/* Detach threads */
	for(std::thread &acceptWorker : acceptThreads)
		acceptWorker.detach();

	/* Increase the instance */
	std::cout << "[+] TCPserver #" << serverInstances << " is now running...\n";
	m_serverStarted = true;

	return 0;
}

bool net::TCPserver::hasStarted(void)
{
	return m_serverStarted;
}

bool net::TCPserver::hasToShutdown(void)
{
	return m_shutdownTCPservers;
}

void net::TCPserver::signalHandler(int signalNum)
{
	if(signalNum == SIGPIPE) {
		std::cout << "[*] SIGPIPE caught...\n";
		return;
	}

	if(signalNum == SIGKILL) exit(signalNum);

	if(m_shutdownTCPservers == true) {
		std::cout << "[+] Terminating the server by force...\n";
		exit(signalNum);
	}
	else {
		std::cout << "[+] Grafully shuting down the server...\n";
		m_shutdownTCPservers = true;
		m_intSigCond.notify_one();
		std::this_thread::sleep_for(std::chrono::seconds(2));
	}
}

void net::TCPserverThreadCore(std::shared_ptr<net::TCPserver> _server)
{
	static int threadCount = 0;
	net::SOCKET remoteSock;
	std::string callbacksID;
	std::thread* thr;

	threadCount += 1;

	net::TCPserver *server = _server.get();

	net::TCPpeer *peer;

	while(true) {
		if(server->hasToShutdown()) return;

		try {;
			/* Accept incoming */
			peer = server->accept();
			/* */
            peer->flags(SET_WILL_CLOSE_SOCKET, 0);
		} catch(net::SocketException& e) {
			e.display();
			continue;
		} catch(...) {
			std::cout << "\n[*] This behavior needs to be investigated. \n\n[*] High Priority!\n";
			server->signalHandler(SIGKILL);
		}

		if(!peer->isValid()) {
            		std::cout << "[*] Failed to accept connection...\n";
            		continue;
		}
        else {
            /* peer object */
            peer->serverCallbacks = server->serverCallbacks;

            if(dynamic_cast<callbacks::ServerCode*>(peer->serverCallbacks)) {
                callbacks::ServerCode* callback =
                        dynamic_cast<callbacks::ServerCode*>(peer->serverCallbacks);
                thr = new std::thread(callback->HandleAndServe, *peer);
                thr->detach();
            }
            else {
                handleConn(*peer);

                peer->shutdown(0);
                peer->close();
            }

            delete peer;
		}
	}
}

void net::handleConn(net::TCPpeer &peer)
{
    std::string req;

    peer >> req;
    std::cout << "From: " << req << '\n';

    peer << "Response from default handler...\r\n\r\n";
}

void net::wait(void)
{
	std::mutex m ;
	std::unique_lock<std::mutex> lock(m);
	TCPserver::m_intSigCond.wait(lock, []{return TCPserver::m_shutdownTCPservers;});
}
