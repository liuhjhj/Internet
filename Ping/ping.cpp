#include <iostream>
#include <winsock2.h>
#include <ws2tcpip.h>
#include <ping.h>
#include <cstdio>
using namespace std;

//IP 报头
typedef struct
{
    unsigned char hdr_len : 4;     //4 位头部长度
    unsigned char version : 4;     //4 位版本号
    unsigned char tos;             //8 位服务类型
    unsigned short total_len;      //16 位总长度
    unsigned short identifier;     //16 位标识符
    unsigned short frag_and_flags; //3 位标志加 13 位片偏移
    unsigned char ttl;             //8 位生存时间
    unsigned char protocol;        //8 位上层协议号
    unsigned short checksum;       //16 位校验和
    unsigned long sourceIP;        //32 位源 IP 地址
    unsigned long destIP;          //32 位目的 IP 地址
} IP_HEADER;
//ICMP 报头
typedef struct
{
    BYTE type;    //8 位类型字段
    BYTE code;    //8 位代码字段
    USHORT cksum; //16 位校验和
    USHORT id;    //16 位标识符
    USHORT seq;   //16 位序列号
} ICMP_HEADER;
//报文解码结构
typedef struct
{
    USHORT usSeqNo;        //序列号
    DWORD dwRoundTripTime; //往返时间
    in_addr dwIPaddr;      //返回报文的 IP 地址
} DECODE_RESULT;
//计算网际校验和函数
USHORT checksum(USHORT *pBuf, int iSize)
{
    unsigned long cksum = 0;
    while (iSize > 1)
    {
        cksum += *pBuf++;
        iSize -= sizeof(USHORT);
    }
    if (iSize)
    {
        cksum += *(UCHAR *)pBuf;
    }
    cksum = (cksum >> 16) + (cksum & 0xffff);
    cksum += (cksum >> 16);
    return (USHORT)(~cksum);
}
//对数据包进行解码
BOOL DecodeIcmpResponse(char *pBuf, int iPacketSize, DECODE_RESULT &DecodeResult, BYTE ICMP_ECHO_REPLY, BYTE ICMP_TIMEOUT)
{
    //检查数据报大小的合法性
    IP_HEADER *pIpHdr = (IP_HEADER *)pBuf;
    int iIpHdrLen = pIpHdr->hdr_len * 4;
    if (iPacketSize < (int)(iIpHdrLen + sizeof(ICMP_HEADER)))
    {
        return FALSE;
    }
    //根据 ICMP 报文类型提取 ID 字段和序列号字段
    ICMP_HEADER *pIcmpHdr = (ICMP_HEADER *)(pBuf + iIpHdrLen);
    USHORT usID, usSquNo;
    if (pIcmpHdr->type == ICMP_ECHO_REPLY) //ICMP 回显应答报文
    {
        usID = pIcmpHdr->id;     //报文 ID
        usSquNo = pIcmpHdr->seq; //报文序列号
    }
    else if (pIcmpHdr->type == ICMP_TIMEOUT) //ICMP 超时差错报文
    {
        char *pInnerIpHdr = pBuf + iIpHdrLen + sizeof(ICMP_HEADER);                 //载荷中的 IP 头
        int iInnerIPHdrLen = ((IP_HEADER *)pInnerIpHdr)->hdr_len * 4;               //载荷中的 IP 头长
        ICMP_HEADER *pInnerIcmpHdr = (ICMP_HEADER *)(pInnerIpHdr + iInnerIPHdrLen); //载荷中的 ICMP 头
        usID = pInnerIcmpHdr->id;                                                   //报文 ID
        usSquNo = pInnerIcmpHdr->seq;                                               //序列号
    }
    else
    {
        return false;
    }
    //检查 ID 和序列号以确定收到期待数据报
    if (usID != (USHORT)GetCurrentProcessId() || usSquNo != DecodeResult.usSeqNo)
    {
        return false;
    }
    //记录 IP 地址并计算往返时间
    DecodeResult.dwIPaddr.s_addr = pIpHdr->sourceIP;
    DecodeResult.dwRoundTripTime = GetTickCount() - DecodeResult.dwRoundTripTime;
    //处理正确收到的 ICMP 数据报
    if (pIcmpHdr->type == ICMP_ECHO_REPLY || pIcmpHdr->type == ICMP_TIMEOUT)
    {
        //输出往返时间信息
        if (DecodeResult.dwRoundTripTime)
            cout << " " << DecodeResult.dwRoundTripTime << "ms" << flush;
        else
            cout << " "
                 << "<1ms" << flush;
    }
    return true;
}

void ping::run()
{
    WSADATA wsa;
    WSAStartup(MAKEWORD(2,2), &wsa);
    char IpAddress[255];
    sockaddr_in destSockAddr;
    ZeroMemory(&destSockAddr, sizeof (sockaddr_in));
    destSockAddr.sin_family = AF_INET;
    SOCKET sockRaw = WSASocket(AF_INET, SOCK_RAW, IPPROTO_ICMP, NULL, 0, WSA_FLAG_OVERLAPPED);
    //接收超时
    setsockopt(sockRaw, SOL_SOCKET, SO_RCVTIMEO, (char *)&iTimeout, sizeof(iTimeout));
    //发送超时
    setsockopt(sockRaw, SOL_SOCKET, SO_SNDTIMEO, (char *)&iTimeout, sizeof(iTimeout));
    //构造 ICMP 回显请求消息，并以 TTL 递增的顺序发送报文
    //ICMP 类型字段
    const BYTE ICMP_ECHO_REQUEST = 8; //请求回显
    const BYTE ICMP_ECHO_REPLY = 0;   //回显应答
    const BYTE ICMP_TIMEOUT = 11;     //传输超时
    //其他常量定义
    const int DEF_ICMP_DATA_SIZE = 32;     //ICMP 报文默认数据字段长度
    const int MAX_ICMP_PACKET_SIZE = 1024; //ICMP 报文最大长度（包括报头）
    const DWORD DEF_ICMP_TIMEOUT = 3000;   //回显应答超时时间
    const int DEF_MAX_HOP = 30;            //最大跳站数
    //填充 ICMP 报文中每次发送时不变的字段
    char IcmpSendBuf[sizeof(ICMP_HEADER) + DEF_ICMP_DATA_SIZE]; //发送缓冲区
    memset(IcmpSendBuf, 0, sizeof(IcmpSendBuf));                //初始化发送缓冲区
    char IcmpRecvBuf[MAX_ICMP_PACKET_SIZE];                     //接收缓冲区
    memset(IcmpRecvBuf, 0, sizeof(IcmpRecvBuf));                //初始化接收缓冲区
    ICMP_HEADER *pIcmpHeader = (ICMP_HEADER *)IcmpSendBuf;
    pIcmpHeader->type = ICMP_ECHO_REQUEST;                              //类型为请求回显
    pIcmpHeader->code = 0;                                              //代码字段为 0
    pIcmpHeader->id = (USHORT)GetCurrentProcessId();                    //ID 字段为当前进程号
    memset(IcmpSendBuf + sizeof(ICMP_HEADER), 'E', DEF_ICMP_DATA_SIZE); //数据字段
    USHORT usSeqNo = 0;                                                 //ICMP 报文序列号
    int iTTL = 64;                                                      //TTL 初始值为 1
    int startAddr, endAddr;
    startAddr = addr4;
    endAddr = addr5;
    DECODE_RESULT DecodeResult;
    int row = 0;
    int col = 0;
    for(; startAddr <= endAddr; startAddr++)
    {
        memset(IpAddress, 0, sizeof(IpAddress));
        snprintf(IpAddress, sizeof (IpAddress), "%d.%d.%d.%d", addr1, addr2, addr3, startAddr);
        cout << "IpAddress is:" << IpAddress << endl;
        u_long ulDestIp = inet_addr(IpAddress);
        if(ulDestIp == INADDR_NONE)
        {
            return;
        }
        destSockAddr.sin_addr.s_addr = ulDestIp;
        //设置 IP 报头的 TTL 字段
        setsockopt(sockRaw, IPPROTO_IP, IP_TTL, (char *)&iTTL, sizeof(iTTL));
        //填充 ICMP 报文中每次发送变化的字段
        ((ICMP_HEADER *)IcmpSendBuf)->cksum = 0;              //校验和先置为 0
        ((ICMP_HEADER *)IcmpSendBuf)->seq = htons(usSeqNo++); //填充序列号
        ((ICMP_HEADER *)IcmpSendBuf)->cksum = checksum((USHORT *)IcmpSendBuf,
                                                       sizeof(ICMP_HEADER) + DEF_ICMP_DATA_SIZE); //计算校验和
        //记录序列号和当前时间
        DecodeResult.usSeqNo = ((ICMP_HEADER *)IcmpSendBuf)->seq; //当前序号
        DecodeResult.dwRoundTripTime = GetTickCount();            //当前时间
                                                                  //发送 TCP 回显请求信息
        sendto(sockRaw, IcmpSendBuf, sizeof(IcmpSendBuf), 0, (sockaddr *)&destSockAddr, sizeof(destSockAddr));
        //接收 ICMP 差错报文并进行解析处理
        sockaddr_in from;            //对端 socket 地址
        int iFromLen = sizeof(from); //地址结构大小
        int iReadDataLen;            //接收数据长度
        //接收数据
        iReadDataLen = recvfrom(sockRaw, IcmpRecvBuf, MAX_ICMP_PACKET_SIZE, 0, (sockaddr *)&from, &iFromLen);
        while(1){
            if (iReadDataLen != SOCKET_ERROR) //有数据到达
            {
                //对数据包进行解码
                if (DecodeIcmpResponse(IcmpRecvBuf, iReadDataLen, DecodeResult, ICMP_ECHO_REPLY, ICMP_TIMEOUT))
                {
                    //输出 IP 地址
                    emit setTable(row, col, inet_ntoa(DecodeResult.dwIPaddr));
                    row++;
                    cout << "\tsuccess\n" << endl;
                    break;
                }
            }
            else if (WSAGetLastError() == WSAETIMEDOUT) //接收超时，输出*号
            {
                cout << " *" << '\t' << "Request timed out.\n" << endl;
                break;
            }
            else
            {
                break;
            }
        }
    }
}
