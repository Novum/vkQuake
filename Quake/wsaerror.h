
/* strings for winsock error codes.
 * from online references, such as
 * http://aluigi.org/mytoolz/winerr.h
 * http://www.winsock-error.com or
 * http://www.sockets.com/err_lst1.htm
 */

#ifndef __wsaerr_static
#define __wsaerr_static static
#endif	/* static */

__wsaerr_static const char *__WSAE_StrError (int err)
{
	switch (err)
	{
	case 0:			return "No error";
	case WSAEINTR:		return "Interrupted system call";		/* 10004 */
	case WSAEBADF:		return "Bad file number";			/* 10009 */
	case WSAEACCES:		return "Permission denied";			/* 10013 */
	case WSAEFAULT:		return "Bad address";				/* 10014 */
	case WSAEINVAL:		return "Invalid argument (not bind)";		/* 10022 */
	case WSAEMFILE:		return "Too many open files";			/* 10024 */
	case WSAEWOULDBLOCK:	return "Operation would block";			/* 10035 */
	case WSAEINPROGRESS:	return "Operation now in progress";		/* 10036 */
	case WSAEALREADY:	return "Operation already in progress";		/* 10037 */
	case WSAENOTSOCK:	return "Socket operation on non-socket";	/* 10038 */
	case WSAEDESTADDRREQ:	return "Destination address required";		/* 10039 */
	case WSAEMSGSIZE:	return "Message too long";			/* 10040 */
	case WSAEPROTOTYPE:	return "Protocol wrong type for socket";	/* 10041 */
	case WSAENOPROTOOPT:	return "Bad protocol option";			/* 10042 */
	case WSAEPROTONOSUPPORT: return "Protocol not supported";		/* 10043 */
	case WSAESOCKTNOSUPPORT: return "Socket type not supported";		/* 10044 */
	case WSAEOPNOTSUPP:	return "Operation not supported on socket";	/* 10045 */
	case WSAEPFNOSUPPORT:	return "Protocol family not supported";		/* 10046 */
	case WSAEAFNOSUPPORT:	return "Address family not supported by protocol family"; /* 10047 */
	case WSAEADDRINUSE:	return "Address already in use";		/* 10048 */
	case WSAEADDRNOTAVAIL:	return "Can't assign requested address";	/* 10049 */
	case WSAENETDOWN:	return "Network is down";			/* 10050 */
	case WSAENETUNREACH:	return "Network is unreachable";		/* 10051 */
	case WSAENETRESET:	return "Net dropped connection or reset";	/* 10052 */
	case WSAECONNABORTED:	return "Software caused connection abort";	/* 10053 */
	case WSAECONNRESET:	return "Connection reset by peer";		/* 10054 */
	case WSAENOBUFS:	return "No buffer space available";		/* 10055 */
	case WSAEISCONN:	return "Socket is already connected";		/* 10056 */
	case WSAENOTCONN:	return "Socket is not connected";		/* 10057 */
	case WSAESHUTDOWN:	return "Can't send after socket shutdown";	/* 10058 */
	case WSAETOOMANYREFS:	return "Too many references, can't splice";	/* 10059 */
	case WSAETIMEDOUT:	return "Connection timed out";			/* 10060 */
	case WSAECONNREFUSED:	return "Connection refused";			/* 10061 */
	case WSAELOOP:		return "Too many levels of symbolic links";	/* 10062 */
	case WSAENAMETOOLONG:	return "File name too long";			/* 10063 */
	case WSAEHOSTDOWN:	return "Host is down";				/* 10064 */
	case WSAEHOSTUNREACH:	return "No Route to Host";			/* 10065 */
	case WSAENOTEMPTY:	return "Directory not empty";			/* 10066 */
	case WSAEPROCLIM:	return "Too many processes";			/* 10067 */
	case WSAEUSERS:		return "Too many users";			/* 10068 */
	case WSAEDQUOT:		return "Disc Quota Exceeded";			/* 10069 */
	case WSAESTALE:		return "Stale NFS file handle";			/* 10070 */
	case WSAEREMOTE:	return "Too many levels of remote in path";	/* 10071 */
	case WSAEDISCON:	return "Graceful shutdown in progress";		/* 10101 */

	case WSASYSNOTREADY:	return "Network SubSystem is unavailable";			/* 10091 */
	case WSAVERNOTSUPPORTED: return "WINSOCK DLL Version out of range";			/* 10092 */
	case WSANOTINITIALISED:	return "Successful WSASTARTUP not yet performed";		/* 10093 */
	case WSAHOST_NOT_FOUND:	return "Authoritative answer: Host not found";			/* 11001 */
	case WSATRY_AGAIN:	return "Non-Authoritative: Host not found or SERVERFAIL";	/* 11002 */
	case WSANO_RECOVERY:	return "Non-Recoverable errors, FORMERR, REFUSED, NOTIMP";	/* 11003 */
	case WSANO_DATA:	return "Valid name, no data record of requested type";		/* 11004 */

	case WSAENOMORE:		return "10102: No more results";			/* 10102 */
	case WSAECANCELLED:		return "10103: Call has been canceled";			/* 10103 */
	case WSAEINVALIDPROCTABLE:	return "Procedure call table is invalid";		/* 10104 */
	case WSAEINVALIDPROVIDER:	return "Service provider is invalid";			/* 10105 */
	case WSAEPROVIDERFAILEDINIT:	return "Service provider failed to initialize";		/* 10106 */
	case WSASYSCALLFAILURE:		return "System call failure";				/* 10107 */
	case WSASERVICE_NOT_FOUND:	return "Service not found";				/* 10108 */
	case WSATYPE_NOT_FOUND:		return "Class type not found";				/* 10109 */
	case WSA_E_NO_MORE:		return "10110: No more results";			/* 10110 */
	case WSA_E_CANCELLED:		return "10111: Call was canceled";			/* 10111 */
	case WSAEREFUSED:		return "Database query was refused";			/* 10112 */

	default:
		{
			static char _err_unknown[64];
			sprintf(_err_unknown, "Unknown WSAE error (%d)", err);
			return  _err_unknown;
		}
	}
}

