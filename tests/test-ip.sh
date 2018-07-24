#!/bin/bash -
# nbdkit
# Copyright (C) 2016-2018 Red Hat Inc.
# All rights reserved.
#
# Redistribution and use in source and binary forms, with or without
# modification, are permitted provided that the following conditions are
# met:
#
# * Redistributions of source code must retain the above copyright
# notice, this list of conditions and the following disclaimer.
#
# * Redistributions in binary form must reproduce the above copyright
# notice, this list of conditions and the following disclaimer in the
# documentation and/or other materials provided with the distribution.
#
# * Neither the name of Red Hat nor the names of its contributors may be
# used to endorse or promote products derived from this software without
# specific prior written permission.
#
# THIS SOFTWARE IS PROVIDED BY RED HAT AND CONTRIBUTORS ''AS IS'' AND
# ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO,
# THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A
# PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL RED HAT OR
# CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
# SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
# LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF
# USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND
# ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY,
# OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT
# OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
# SUCH DAMAGE.

# Every other test uses a Unix domain socket.  This tests nbdkit over
# IPv4 and IPv6 localhost connections.

set -e
source ./functions.sh

rm -f ip.pid ipv4.out ipv6.out

# Don't fail if certain commands aren't available.
if ! ss --version; then
    echo "$0: 'ss' command not available"
    exit 77
fi
if ! command -v qemu-img; then
    echo "$0: 'qemu-img' command not available"
    exit 77
fi
if ! qemu-img --help | grep -- --image-opts; then
    echo "$0: 'qemu-img' command does not have the --image-opts option"
    exit 77
fi
if ! ip -V; then
    echo "$0: 'ip' command not available"
    exit 77
fi

# Find an unused port to listen on.
for port in `seq 49152 65535`; do
    if ! ss -ltn | grep -sqE ":$port\b"; then break; fi
done
echo picked unused port $port

# By default nbdkit will listen on all available interfaces, ie.
# IPv4 and IPv6.
nbdkit -P ip.pid -p $port example1

# We may have to wait a short time for the pid file to appear.
for i in `seq 1 10`; do
    if test -f ip.pid; then
        break
    fi
    sleep 1
done
if ! test -f ip.pid; then
    echo "$0: PID file was not created"
    exit 1
fi

pid="$(cat ip.pid)"

# Check the process exists.
kill -s 0 $pid

# Check we can connect over the IPv4 loopback interface.
ipv4_lo="$(ip -o -4 addr show scope host)"
if test -n "$ipv4_lo"; then
    qemu-img info --image-opts "file.driver=nbd,file.host=127.0.0.1,file.port=$port" > ipv4.out
    cat ipv4.out
    grep -sq "^virtual size: 100M" ipv4.out
fi

# Check we can connect over the IPv6 loopback interface.
ipv6_lo="$(ip -o -6 addr show scope host)"
if test -n "$ipv6_lo"; then
    qemu-img info --image-opts "file.driver=nbd,file.host=::1,file.port=$port" > ipv6.out
    cat ipv6.out
    grep -sq "^virtual size: 100M" ipv6.out
fi

# Kill the process.
kill $pid
rm ip.pid
rm -f ipv4.out ipv6.out