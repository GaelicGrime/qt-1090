#
/*
 *      The http handling in SDRunoPlugin_1090 is
 *	based on and contains source code from dump1090
 *      Copyright (C) 2012 by Salvatore Sanfilippo <antirez@gmail.com>
 *      all rights acknowledged.
 *
 *	Copyright (C) 2018
 *	Jan van Katwijk (J.vanKatwijk@gmail.com)
 *	Lazy Chair Computing
 *
 *	This file is part of the SDRunoPlugin_1090
 *
 *    SDRunoPlugin_1090 is free software; you can redistribute it and/or modify
 *    it under the terms of the GNU General Public License as published by
 *    the Free Software Foundation; either version 2 of the License, or
 *    (at your option) any later version.
 *
 *    SDRunoPlugin_1090 is distributed in the hope that it will be useful,
 *    but WITHOUT ANY WARRANTY; without even the implied warranty of
 *    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *    GNU General Public License for more details.
 *
 *    You should have received a copy of the GNU General Public License
 *    along with SDRunoPlugin_1090; if not, write to the Free Software
 *    Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 */

#include	<stdio.h>
#include	<stdlib.h>
#include	<unistd.h>
#include	<sys/types.h> 
#ifndef	__MINGW32__
#include	<sys/socket.h>
#include	<fcntl.h>
#include	<netinet/in.h>
#include	<netdb.h>
#include	<arpa/inet.h>
#else
#include        <winsock2.h>
#include        <windows.h>
#include        <ws2tcpip.h>
#endif
#include	"http-handler.h"
#include	"qt-1090.h"
#include	"aircraft-handler.h"

#include	"converted_map.h"

	httpHandler::httpHandler (qt1090 *parent,
	                          std::complex<float> homeAddress,
	                          const QString &mapPort,
	                          bool autoBrowser) {
	                          
	this	-> parent	= parent;
	this	-> homeAddress	= homeAddress;
	this	-> mapPort	= mapPort;
	this	-> autoBrowser	= autoBrowser;
	this	-> running. store (false);

	QString	temp	= QString ("http://localhost") + ":" + mapPort;
#ifdef  __MINGW32__
        this    -> browserAddress       = temp. toStdWString ();
#else
        this    -> browserAddress       = temp. toStdString ();
#endif
}

	httpHandler::~httpHandler	() {
	if (running. load ()) {
	   running. store (false);
	   threadHandle. join ();
	}
}

void	httpHandler::start	() {
	threadHandle = std::thread (&httpHandler::run, this);
	if (!autoBrowser)
	   return;
#ifdef	__MINGW32__
	ShellExecute (NULL, L"open", browserAddress. c_str (),
	                                   NULL, NULL, SW_SHOWNORMAL);
#else
	std::string x = "xdg-open " + browserAddress;
	system (x. c_str ());
#endif
}

void	httpHandler::stop	() {
	if (running. load ()) {
           running. store (false);
           threadHandle. join ();
        }
}

#ifndef	__MINGW32__
void	httpHandler::run	() {
char	buffer [4096];
bool	keepalive;
char	*url;
int one = 1, clientSocket, listenSocket;
struct sockaddr_in svr_addr, cli_addr;
std::string	content;
std::string	ctype;

	running. store (true);
	socklen_t sin_len = sizeof (cli_addr);
	listenSocket = socket (AF_INET, SOCK_STREAM, 0);
	if (listenSocket < 0) {
	   running. store (false);
	   return;
	}

	int flags	= fcntl (listenSocket, F_GETFL);
        fcntl (listenSocket, F_SETFL, flags | O_NONBLOCK);
	setsockopt (listenSocket, SOL_SOCKET, SO_REUSEADDR, &one, sizeof(int));
	svr_addr.sin_family = AF_INET;
	svr_addr.sin_addr.s_addr = INADDR_ANY;
	svr_addr.sin_port = htons (mapPort. toInt ());
 
	if (bind (listenSocket, (struct sockaddr *) &svr_addr,
	                                  sizeof(svr_addr)) == -1) {
	   close (listenSocket);
	   running. store (false);
	   return;
	}

//
//      Now, we are listening to port XXXX, ready to accept a
//      socket for anyone who needs us

        ::listen (listenSocket, 5);

	while (running. load ()) {
	   clientSocket = accept (listenSocket,
	                    (struct sockaddr *) &cli_addr, &sin_len);
	   if (clientSocket == -1) {
	      usleep (2000000);
	      continue;
	   }

//
//      someone needs us, let us see what (s)he wants
           while (running. load ()) {
              if (read (clientSocket, buffer, 4096) < 0) {
                 running. store (false);
                 break;
              }

//	      fprintf (stderr, "%s\n", buffer);
	      int httpver = (strstr (buffer, "HTTP/1.1") != nullptr) ? 11 : 10;
	      if (httpver == 11) 
//	HTTP 1.1 defaults to keep-alive, unless close is specified. 
	         keepalive = strstr (buffer, "Connection: close") == nullptr;
	      else // httpver == 10
	         keepalive = strstr (buffer, "Connection: keep-alive") != nullptr;

/*	Identify the URL. */
	      char *p = strchr (buffer,' ');
	      if (p == nullptr) 
	         break;
	      url = ++p; // Now this should point to the requested URL. 
	      p = strchr (p, ' ');
	      if (p == nullptr)
	         break;
	      *p = '\0';

//	Select the content to send, we have just two so far:
//	 "/" -> Our google map application.
//	 "/data.json" -> Our ajax request to update planes. */
	   bool jsonUpdate	= false;
	      if (strstr (url, "/data.json")) {
	         QString xx	= aircraftsToJson (parent -> planeList);
	         content	= xx. toStdString ();
                 ctype		= "application/json;charset=utf-8";
	         jsonUpdate	= true;
	      }
	      else {
	         content	= theMap (homeAddress);
	         ctype		= "text/html;charset=utf-8";
	      }

//	Create the header 
	      char hdr [2048];
	      sprintf (hdr,
	               "HTTP/1.1 200 OK\r\n"
	               "Server: dump1090\r\n"
	               "Content-Type: %s\r\n"
	               "Connection: %s\r\n"
	               "Content-Length: %d\r\n"
//	               "Access-Control-Allow-Origin: *\r\n"
	               "\r\n",
	               ctype. c_str (),
	               keepalive ? "keep-alive" : "close",
	               (int)(strlen (content. c_str ())));
	      int hdrlen = strlen (hdr);
//	      fprintf (stderr, "%s \n", hdr);
	      if (jsonUpdate) {
//	         fprintf (stderr, "Json update requested\n");
//	         fprintf (stderr, "%s\n", content. c_str ());
	      }
//	and send the reply
	      if (write (clientSocket, hdr, hdrlen) != hdrlen ||
	          write (clientSocket, content. c_str (),
	                          content. size ()) != content. size ())  {
	         fprintf (stderr, "WRITE PROBLEM\n");
	         break;
	      }
	   }
	}
}
#else
//
//	windows version
//
void	httpHandler::run	() {
char	buffer [4096];
bool	keepalive;
char	*url;
std::string	content;
std::string	ctype;
WSADATA	wsa;
int	iResult;
SOCKET	ListenSocket	= INVALID_SOCKET;
SOCKET	ClientSocket	= INVALID_SOCKET;

struct addrinfo *result = nullptr;
struct addrinfo hints;

	if (WSAStartup (MAKEWORD (2, 2), &wsa) != 0) {
	   return;
	}

	ZeroMemory (&hints, sizeof(hints));
	hints.ai_family		= AF_INET;
	hints.ai_socktype	= SOCK_STREAM;
	hints.ai_protocol	= IPPROTO_TCP;
	hints.ai_flags		= AI_PASSIVE;

//	Resolve the server address and port

	iResult = getaddrinfo (nullptr, mapPort. toLatin1 (). data (),
	                                               &hints, &result);
	if (iResult != 0 ) {
	       WSACleanup();
	   return;
	}

// Create a SOCKET for connecting to server
	ListenSocket = socket (result -> ai_family,
	                       result -> ai_socktype, result -> ai_protocol);
	if (ListenSocket == INVALID_SOCKET) {
	   freeaddrinfo (result);
	   WSACleanup ();
	   return;
	}
	unsigned long mode = 1;
	ioctlsocket (ListenSocket, FIONBIO, &mode);

// Setup the TCP listening socket
	iResult = bind (ListenSocket, result -> ai_addr,
	                                (int)result->ai_addrlen);
	if (iResult == SOCKET_ERROR) {
	   freeaddrinfo (result);
	   closesocket (ListenSocket);
	   WSACleanup ();
	   return;
	}

	freeaddrinfo (result);
	running. store (true);

	listen (ListenSocket, 5);
	while (running. load ()) {
	   ClientSocket = accept (ListenSocket, nullptr, nullptr);
	   if (ClientSocket == -1)  {
	      usleep (2000000);
	      continue;
	   }

	   while (running. load ()) {
	      int xx;
L1:	      if ((xx = recv (ClientSocket, buffer, 4096, 0)) < 0) {
// shutdown the connection since we're done
	         iResult = shutdown (ClientSocket, SD_SEND);
	         if (iResult == SOCKET_ERROR) {
	            closesocket (ClientSocket);
	            closesocket (ListenSocket);
	            WSACleanup ();
	            running.store (false);
	            return;
	         }
	         break;
	      }
	      if (xx == 0) {
	         if (!running. load ()) {
	            closesocket (ClientSocket);
	            closesocket(ListenSocket);
	            WSACleanup ();
	            return;
	         }
	         Sleep (1);
	         goto L1;
	      }

	      int httpver = (strstr (buffer, "HTTP/1.1") != NULL) ? 11 : 10;
	      if (httpver == 11)
//	HTTP 1.1 defaults to keep-alive, unless close is specified.
	         keepalive = strstr (buffer, "Connection: close") == NULL;
	      else // httpver == 10
	         keepalive = strstr (buffer, "Connection: keep-alive") != NULL;

	/* Identify the URL. */
	      char *p = strchr (buffer, ' ');
	      if (p == NULL)
	         break;
	      url = ++p; // Now this should point to the requested URL.
	      p = strchr (p, ' ');
	      if (p == NULL)
	         break;
	      *p = '\0';

//	Select the content to send, we have just two so far:
//	 "/" -> Our google map application.
//	 "/data.json" -> Our ajax request to update transmitters. */
	      bool jsonUpdate	= false;
	      if (strstr (url, "/data.json")) {
	         QString xx	= aircraftsToJson (parent -> planeList);
	         content	= xx. toStdString ();
	         if (content != "") {
	            ctype	= "application/json;charset=utf-8";
	            jsonUpdate	= true;
//	            fprintf (stderr, "%s will be sent\n", content. c_str ());
	         }
	      }
	      else {
	         content	= theMap (homeAddress);
	         ctype		= "text/html;charset=utf-8";
	      }
//	Create the header
	      char hdr [2048];
	      sprintf (hdr,
	               "HTTP/1.1 200 OK\r\n"
	               "Server: SDRunoPlugin_1090\r\n"
	               "Content-Type: %s\r\n"
	               "Connection: %s\r\n"
	               "Content-Length: %d\r\n"
//	               "Access-Control-Allow-Origin: *\r\n"
	               "\r\n",
	               ctype. c_str (),
	               keepalive ? "keep-alive" : "close",
	               (int)(strlen (content. c_str ())));
	      int hdrlen = strlen (hdr);
//	      if (jsonUpdate) {
//	         parent -> show_text (std::string ("Json update requested\n"));
//	         parent -> show_text (content);
//	      }
//	and send the reply
	      if ((send (ClientSocket, hdr, hdrlen, 0) == SOCKET_ERROR) ||
	          (send (ClientSocket, content. c_str (),
	                          content. size (), 0) == SOCKET_ERROR))  {
	         fprintf (stderr, "WRITE PROBLEM\n");
	         break;
	      }
	   }
	}
// cleanup
	closesocket(ClientSocket);
	closesocket(ListenSocket);
	WSACleanup();
	running. store (false);
}
#endif

std::string	httpHandler::theMap (std::complex<float> homeAddress) {
std::string res;
int	bodySize;
char	*body;
std::string latitude	= std::to_string (real (homeAddress));
std::string longitude	= std::to_string (imag (homeAddress));
int	index		= 0;
int	cc;
int teller	= 0;
int params	= 0;

	bodySize	= sizeof (qt_map);
	body		=  (char *)malloc (bodySize + 40);
	while (qt_map [index] != 0) {
	   cc =  (char)(qt_map [index]);
	   if (cc == '$') {
	      if (params == 0) {
	         for (int i = 0; latitude. c_str () [i] != 0; i ++)
	            if (latitude. c_str () [i] == ',')
	               body [teller ++] = '.';
	            else
	               body [teller ++] = latitude. c_str () [i];
	         params ++;
	      }
	      else
	      if (params == 1) {
	         for (int i = 0; longitude. c_str () [i] != 0; i ++)
	            if (longitude. c_str () [i] == ',')
	               body [teller ++] = '.';
	            else
	               body [teller ++] = longitude. c_str () [i];
	         params ++;
	      }
	      else
	         body [teller ++] = (char)cc;
	   }
	   else
	      body [teller ++] = (char)cc;
	   index ++;
	}
	body [teller ++] = 0;
	res	= std::string (body);
//	fprintf (stderr, "The map :\n%s\n", res. c_str ());
	free (body);
	return res;
}

//std::string	httpHandler::theMap (const QString &fileName) {
//FILE	*fd;
//std::string res;
//int	bodySize;
//char	*body;
//	fd	=  fopen (fileName. toUtf8 (). data (), "r");
//	fseek (fd, 0L, SEEK_END);
//	bodySize	= ftell (fd);
//	fseek (fd, 0L, SEEK_SET);
//        body =  (char *)malloc (bodySize);
//        fread (body, 1, bodySize, fd);
//        fclose (fd);
//	res	= std::string (body);
//	free (body);
//	return res;
//}
//
