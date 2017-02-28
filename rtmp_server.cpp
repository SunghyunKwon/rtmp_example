#include <iostream>
#include <unistd.h>

// RTMP Inlcude
#include <librtmp/rtmp.h>
#include <librtmp/log.h>

#define HTON16(x)   ((x >> 8 & 0xff) | (x << 8 & 0xff00))
#define HTON24(x)   ((x >> 16 & 0xff) | (x << 16 & 0xff0000) | (x & 0xff00))
#define HTON32(x)   ((x >> 24 & 0xff) | (x >> 8 & 0xff00) | \
        (x << 8 & 0xff0000) | (x << 24 & 0xff000000))
#define HTONTIME(x)  ((x >> 16 & 0xff) | (x << 16 & 0xff0000) | \
        (x & 0xff00) | (x & 0xff00000000))

int ReadUnsigned8(FILE *file_pointer, uint32_t *u8) {
    if (fread(u8, 1, 1, file_pointer) != 1) return 0;
    return 1;
}

int ReadUnsigned16(FILE *file_pointer, uint32_t *u16) {
    if (fread(u16, 2, 1, file_pointer) != 1) return 0;
    *u16 = HTON16(*u16);

    return 1;
}

int ReadUnsigned24(FILE *file_pointer, uint32_t *u24) {
    if (fread(u24, 3, 1, file_pointer) != 1) return 0;
    *u24 = HTON24(*u24);

    return 1;
}

int ReadUnsigned32(FILE *file_pointer, uint32_t *u32) {
    if (fread(u32, 4, 1, file_pointer) != 1) return 0;
    *u32 = HTON32(*u32);

    return 1;
}

int PeekUnsigned8(FILE *file_pointer, uint32_t *u8) {
    if (fread(u8, 1, 1, file_pointer) != 1) return 0;
    fseek(file_pointer, -1, SEEK_CUR);

    return 1;
}

int ReadTime(FILE *file_pointer, uint32_t *utime) {
    if (fread(utime, 4, 1, file_pointer) != 1) return 0;
    *utime = HTONTIME(*utime);

    return 1;
}

int RTMPSendPacket(RTMP *rtmp, FILE *file_pointer) {
    int ret = true;

    int next_is_key = 0;
    uint32_t start_time = 0;
    uint32_t now_time = 0;
    long pre_frame_time = 0;
    long last_time = 0;

    uint32_t type = 0;
    uint32_t data_length = 0;
    uint32_t timestamp = 0;
    uint32_t stream_id = 0;
    uint32_t pre_tag_size = 0;

    RTMPPacket *rtmp_packet = NULL;

    // Jump over FLV header
    fseek(file_pointer, 9, SEEK_SET);
    // Jump over previous Tag sizen
    fseek(file_pointer, 4, SEEK_CUR);

    start_time = RTMP_GetTime();

    while (1) {
        now_time = RTMP_GetTime();
        if (((now_time - start_time) < pre_frame_time) && next_is_key) {
            if (pre_frame_time > last_time) {
                RTMP_LogPrintf("Time stamp : %8lu ms\n", pre_frame_time);
                last_time = pre_frame_time;
            }

            sleep(1000);
            continue;
        }

        // Not quite the same as FLV spec
        if (!ReadUnsigned8(file_pointer, &type)) break;
        if (!ReadUnsigned24(file_pointer, &data_length)) break;
        if (!ReadTime(file_pointer, &timestamp)) break;
        if (!ReadUnsigned24(file_pointer, &stream_id)) break;

        if (type != 0x08 && type != 0x09) {
            // Jump over non_audio and non_video frame.
            // Jump over next previous tag at the same time
            fseek(file_pointer, data_length + 4, SEEK_CUR);
            continue;
        }

        if (fread(rtmp_packet->m_body, 1, data_length, file_pointer) != data_length)
            break;

        rtmp_packet->m_headerType = RTMP_PACKET_SIZE_LARGE;
        rtmp_packet->m_nTimeStamp = timestamp;
        rtmp_packet->m_packetType = type;
        rtmp_packet->m_nBodySize = data_length;
        pre_frame_time = timestamp;

        ret = RTMP_IsConnected(rtmp);
        if (!ret) {
            RTMP_LogPrintf("RTMP_IsConnected failed : [%d]\n", ret);
            goto FINISH_OFF;
        }

        ret = RTMP_SendPacket(rtmp, rtmp_packet, 0);
        if (!ret) {
            RTMP_LogPrintf("RTMP_SendPacket failed : [%d]\n", ret);
            goto FINISH_OFF;
        }

        if (!ReadUnsigned32(file_pointer, &pre_tag_size)) break;
        if (!PeekUnsigned8(file_pointer, &type)) break;
        if (type == 0x09) {
            if (fseek(file_pointer, 11, SEEK_CUR) != 0) break;
            if (!PeekUnsigned8(file_pointer, &type)) break;
            if (type == 0x17) 
                next_is_key = true;
            else
                next_is_key = false;

            fseek(file_pointer, -11, SEEK_CUR);
        }            
    }

    RTMP_LogPrintf("Send Data Over\n");

FINISH_OFF:

    return ret;
}

int RTMPServerCreate(char *filename) {
    int ret = 0;

    FILE *fp = NULL;

    // RTMP Variable
    RTMP *rtmp = NULL;

    fp = fopen(filename, "rb");
    if (fp == NULL) {
        RTMP_LogPrintf("%s file open failed : Error\n", filename);
        goto FINISH_OFF;
    }

    rtmp = RTMP_Alloc();
    if (rtmp == NULL) {
        RTMP_LogPrintf("RTMP_Alloc failed\n");
        goto FINISH_OFF;
    }

    RTMP_Init(rtmp);

    // Set RTMP Variable
    ret = RTMP_SetupURL(rtmp, "rtmp://localhost/publish/live");
    if (!ret) {
        RTMP_LogPrintf("RTMP_SetupURL failed : [%d]\n", ret);
        goto FINISH_OFF;
    }

    RTMP_EnableWrite(rtmp);

    ret = RTMP_Connect(rtmp, NULL);
    if (!ret) {
        RTMP_LogPrintf("RTMP_Connect failed : [%d]\n", ret);
        goto FINISH_OFF;
    }

    ret = RTMP_ConnectStream(rtmp, 0);
    if (!ret) {
        RTMP_LogPrintf("RTMP_ConnectStream failed : [%d]\n", ret);
        goto FINISH_OFF;
    }

    ret = RTMPSendPacket(rtmp, fp);
    if (!ret) {
        RTMP_LogPrintf("RTMPSendPacket failed : [%d]\n", ret);
        goto FINISH_OFF;
    }

FINISH_OFF:
   
    if (fp) fclose(fp);

    if (rtmp) {
        RTMP_Close(rtmp);
        RTMP_Free(rtmp);
    }

    return ret;
}

int main(int argc, char **args) {
    int ret = 0;

    if (argc == 1) {
        RTMP_LogPrintf("usage : rtmp_server [filename]\n");
        goto FINISH_OFF;
    }

    ret = RTMPServerCreate(args[1]);

FINISH_OFF:

    return ret;
}


