#!/bin/bash

show_usage () {
    echo "Usage: $0 <start | stop | restart | check> [tunnum ...]"
    exit 1
}

start_tun () {
    local TUNNUM="$1" TUNDEV="tun$1"
    ip tuntap add mode tun user "${SUDO_USER}" name "${TUNDEV}"
    ip addr add "${TUN_IP_PREFIX}.${TUNNUM}.1/24" dev "${TUNDEV}"
    ip link set dev "${TUNDEV}" up
    ip route change "${TUN_IP_PREFIX}.${TUNNUM}.0/24" dev "${TUNDEV}" rto_min 10ms

    # Apply NAT (masquerading) only to traffic from CS144's network devices
    iptables -t nat -A PREROUTING -s ${TUN_IP_PREFIX}.${TUNNUM}.0/24 -j CONNMARK --set-mark ${TUNNUM}
    iptables -t nat -A POSTROUTING -j MASQUERADE -m connmark --mark ${TUNNUM}
    # 允许ip转接
    echo 1 > /proc/sys/net/ipv4/ip_forward
}

stop_tun () {
    local TUNDEV="tun$1"
    iptables -t nat -D PREROUTING -s ${TUN_IP_PREFIX}.${1}.0/24 -j CONNMARK --set-mark ${1}
    iptables -t nat -D POSTROUTING -j MASQUERADE -m connmark --mark ${1}
    ip tuntap del mode tun name "$TUNDEV"
}

start_all () {
    while [ ! -z "$1" ]; do
        local INTF="$1"; shift
        start_tun "$INTF"
    done
}

stop_all () {
    while [ ! -z "$1" ]; do
        local INTF="$1"; shift
        stop_tun "$INTF"
    done
}

restart_all() {
    stop_all "$@"
    start_all "$@"
}

check_tun () {
    # 和if语句一样的效果，类似于短接，如果参数量不为1，就执行后面的
    [ "$#" != 1 ] && { echo "bad params in check_tun"; exit 1; }
    # 连接字符串
    local TUNDEV="tun${1}"
    # make sure tun is healthy: device is up, ip_forward is set, and iptables is configured
    # 如果ip link show ${TUNDEV} &>/dev/null 正常执行，说明有TUNDEV设备，就不执行后面的return 1
    ip link show ${TUNDEV} &>/dev/null || return 1
    # $(...)命令替换，输出ip_forward的值，看是否启动ip转接，如果启动，就不执行后面的return 2
    [ "$(cat /proc/sys/net/ipv4/ip_forward)" = "1" ] || return 2
}

check_sudo () {
    if [ "$SUDO_USER" = "root" ]; then
        echo "please execute this script as a regular user, not as root"
        exit 1
    fi
    if [ -z "$SUDO_USER" ]; then
        # if the user didn't call us with sudo, re-execute
        exec sudo $0 "$MODE" "$@"
    fi
}

# check arguments
# 如果 第一个参数$1 为空，或者不为后面几个模式，就执行show_usage
if [ -z "$1" ] || ([ "$1" != "start" ] && [ "$1" != "stop" ] && [ "$1" != "restart" ] && [ "$1" != "check" ]); then
    show_usage
fi
MODE=$1; shift

# 如果剩余参数为空,现在设置$1和$2分别为144和145
# set default argument
if [ "$#" = "0" ]; then
    set -- 144 145
fi

# execute 'check' before trying to sudo
# - like start, but exit successfully if everything is OK
# 如果MODE是检查
if [ "$MODE" = "check" ]; then
    # 声明是数组类型
    declare -a INTFS
    # 改成开始模式
    MODE="start"
    # 检查参数1是否为空，参数1为脚本的名字
    while [ ! -z "$1" ]; do
        # 取得第一个参数，并进行移位，后面的参数往前移动
        INTF="$1"; shift
        # 执行函数检查TUN设备
        check_tun ${INTF}
        # 取得函数返回值[0-255]
        RET=$?
        # 返回值为0，说明是正常情况
        if [ "$RET" = "0" ]; then
            continue
        fi

        if [ "$((RET > 1))" = "1" ]; then
            MODE="restart"
        fi
        # 将有问题的设备加入到INTFS中，
        INTFS+=($INTF)
    done

    # address only the interfaces that need it
    set -- "${INTFS[@]}"
    # 如果所有设备都正常就退出
    if [ "$#" = "0" ]; then
        exit 0
    fi
    echo -e "[$0] Bringing up tunnels ${INTFS[@]}:"
fi

# sudo if necessary
check_sudo "$@"

# get configuration
. "$(dirname "$0")"/etc/tunconfig

# start, stop, or restart all intfs
eval "${MODE}_all" "$@"
