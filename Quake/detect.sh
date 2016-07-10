#! /bin/sh

# script from loki_setup tools

DetectARCH()
{
	status=1
	case `uname -m` in
	    amd64 | x86_64)
		echo "x86_64"
		status=0;;
	    i?86 | i86*)
		echo "x86"
		status=0;;
	    90*/*)
		echo "hppa"
		status=0;;
	    *)
		case `uname -s` in
		    IRIX*)
			echo "mips"
			status=0;;
		    AIX*)
			echo "ppc"
			status=0;;
		    *)
			arch=`uname -p 2> /dev/null || uname -m`
			if test "$arch" = powerpc; then
				echo "ppc"
			else
				echo $arch
			fi
			status=0;;
		esac
	esac
	return $status
}

DetectOS()
{
	os=`uname -s`
	if test "$os" = "OpenUNIX"; then
		echo SCO_SV
	else
		echo $os
	fi
	return 0
}

if test "$1" = "os"; then
	result=`DetectOS`
elif test "$1" = "arch"; then
	result=`DetectARCH`
else
	result="OS: `DetectOS`, Arch: `DetectARCH`"
fi

status="$?"
echo $result

exit $status

