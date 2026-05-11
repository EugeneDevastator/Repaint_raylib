#ifndef NET_PROTOCOL_H
#define NET_PROTOCOL_H

#include <stdint.h>
#include <stddef.h>
#include <string.h>

#define NET_PORT 33789
#define NET_HEADER_SIZE 5

enum ConnectionMode {
    emServer,
    emGServer,
    emClient,
    emNone
};

enum dStates {
    sdNONE,
    sdAction,
    sdSection,
    sdLAction,
    sdGetName,
    sdGetPass,
    sdGetMsg,
    sdLogin,
    sdLoginS,
    sdUserAdded,
    sdUserDel,
    sdUserStat,
    sdRoomJoin,
    sdRoomPart,
    sdRoomCreate,
    sdAuth,
    sdReg,
    sdLog,
    sdFriendSeek,
    sdFriendAdd,
    sdFriendDel,
    sdLock,
    sdUnlock,
    sdGetImg,
    sdReqImg,
    sdConfirmImg,
    sdPeopleResults,
    sdMisc,
    sdFAIL,
    sdSTOP
};

enum rStates {
    srNONE, srServ, srClient, srGlobal, srSTOP
};

enum uStates {
    suZERO, suOff, suOn, suRoomFree, suRoomClosed,
    suMemberFree, suMemberClosed, suMemberSame, suSTOP
};

enum aTypes {
    atLogin, atRegister, atRename, atSTOP
};

struct stNetHead {
    uint8_t Hid;
    uint32_t Hsize;

    void Serialize(uint8_t* buf) const {
        buf[0] = Hid;
        buf[1] = (uint8_t)(Hsize >> 24);
        buf[2] = (uint8_t)(Hsize >> 16);
        buf[3] = (uint8_t)(Hsize >> 8);
        buf[4] = (uint8_t)(Hsize);
    }

    void Deserialize(const uint8_t* buf) {
        Hid = buf[0];
        Hsize = ((uint32_t)buf[1] << 24) |
                ((uint32_t)buf[2] << 16) |
                ((uint32_t)buf[3] << 8) |
                (uint32_t)buf[4];
    }
};

struct sAuth {
    char uname[256];
    char upass[256];
    uint8_t aType;

    size_t Serialize(uint8_t* buf, size_t cap) const;
    bool Deserialize(const uint8_t* buf, size_t len);
};

struct stUserState {
    uint8_t Ustate;
    char Uname[256];

    size_t Serialize(uint8_t* buf, size_t cap) const;
    bool Deserialize(const uint8_t* buf, size_t len);
};

static inline size_t SZstring(const char* str, uint8_t* buf, size_t cap) {
    size_t slen = strlen(str);
    if (slen > 65535) slen = 65535;
    if (cap < 2 + slen) slen = cap - 2;
    buf[0] = (uint8_t)(slen >> 8);
    buf[1] = (uint8_t)(slen);
    memcpy(buf + 2, str, slen);
    return 2 + slen;
}

static inline size_t DSZstring_len(const uint8_t* buf, size_t len) {
    if (len < 2) return 0;
    size_t slen = ((size_t)buf[0] << 8) | buf[1];
    if (2 + slen > len) slen = len - 2;
    return slen;
}

static inline size_t SZstring_unpack(const uint8_t* buf, size_t len, char* out, size_t outcap) {
    size_t slen = DSZstring_len(buf, len);
    if (slen >= outcap) slen = outcap - 1;
    memcpy(out, buf + 2, slen);
    out[slen] = '\0';
    return 2 + slen;
}

#endif
