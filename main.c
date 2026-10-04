/*
 * Slimmed Standalone Screengrabber
 * Entfernt: Enigma2 WebIF, HTTP-Streaming-Fallback, XML-Parsing.
 * Beibehalten: Reiner, lokaler Hardware-Screenshot (OSD + Video).
 */

#include <unistd.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <inttypes.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <sys/time.h>
#include <linux/types.h>
#include <linux/fb.h>
#include <stdbool.h>
#include <stdint.h>
#include <dlfcn.h>
#include <dirent.h>
#include <errno.h>
#include <elf.h>

#include "png.h"
#include "jpeglib.h"

#define CLAMP(x)    ((x < 0) ? 0 : ((x > 255) ? 255 : x))
#define SWAP(x,y)	{ x ^= y; y ^= x; x ^= y; }

#define RED565(x)    ((((x) >> (11 )) & 0x1f) << 3)
#define GREEN565(x)  ((((x) >> (5 )) & 0x3f) << 2)
#define BLUE565(x)   ((((x) >> (0)) & 0x1f) << 3)

#define YFB(x)    ((((x) >> (10)) & 0x3f) << 2)
#define CBFB(x)  ((((x) >> (6)) & 0xf) << 4)
#define CRFB(x)   ((((x) >> (2)) & 0xf) << 4)

#define VIDEO_DEV "/dev/video"

static const int yuv2rgbtable_y[256] = {
0xFFED5EA0, 0xFFEE88B6, 0xFFEFB2CC, 0xFFF0DCE2, 0xFFF206F8, 0xFFF3310E, 0xFFF45B24, 0xFFF5853A, 0xFFF6AF50, 0xFFF7D966, 0xFFF9037C, 0xFFFA2D92, 0xFFFB57A8, 0xFFFC81BE, 0xFFFDABD4, 0xFFFED5EA, 0x0, 0x12A16, 0x2542C, 0x37E42, 0x4A858, 0x5D26E, 0x6FC84, 0x8269A, 0x950B0, 0xA7AC6, 0xBA4DC, 0xCCEF2, 0xDF908, 0xF231E, 0x104D34, 0x11774A, 0x12A160, 0x13CB76, 0x14F58C, 0x161FA2, 0x1749B8, 0x1873CE, 0x199DE4, 0x1AC7FA, 0x1BF210, 0x1D1C26, 0x1E463C, 0x1F7052, 0x209A68, 0x21C47E, 0x22EE94, 0x2418AA, 0x2542C0, 0x266CD6, 0x2796EC, 0x28C102, 0x29EB18, 0x2B152E, 0x2C3F44, 0x2D695A, 0x2E9370, 0x2FBD86, 0x30E79C, 0x3211B2, 0x333BC8, 0x3465DE, 0x358FF4, 0x36BA0A, 0x37E420, 0x390E36, 0x3A384C, 0x3B6262, 0x3C8C78, 0x3DB68E, 0x3EE0A4, 0x400ABA, 0x4134D0, 0x425EE6, 0x4388FC, 0x44B312, 0x45DD28, 0x47073E, 0x483154, 0x495B6A, 0x4A8580, 0x4BAF96, 0x4CD9AC, 0x4E03C2, 0x4F2DD8, 0x5057EE, 0x518204, 0x52AC1A, 0x53D630, 0x550046, 0x562A5C, 0x575472, 0x587E88, 0x59A89E, 0x5AD2B4, 0x5BFCCA, 0x5D26E0, 0x5E50F6, 0x5F7B0C, 0x60A522, 0x61CF38, 0x62F94E, 0x642364, 0x654D7A, 0x667790, 0x67A1A6, 0x68CBBC, 0x69F5D2, 0x6B1FE8, 0x6C49FE, 0x6D7414, 0x6E9E2A, 0x6FC840, 0x70F256, 0x721C6C, 0x734682, 0x747098, 0x759AAE, 0x76C4C4, 0x77EEDA, 0x7918F0, 0x7A4306, 0x7B6D1C, 0x7C9732, 0x7DC148, 0x7EEB5E, 0x801574, 0x813F8A, 0x8269A0, 0x8393B6, 0x84BDCC, 0x85E7E2, 0x8711F8, 0x883C0E, 0x896624, 0x8A903A, 0x8BBA50, 0x8CE466, 0x8E0E7C, 0x8F3892, 0x9062A8, 0x918CBE, 0x92B6D4, 0x93E0EA, 0x950B00, 0x963516, 0x975F2C, 0x988942, 0x99B358, 0x9ADD6E, 0x9C0784, 0x9D319A, 0x9E5BB0, 0x9F85C6, 0xA0AFDC, 0xA1D9F2, 0xA30408, 0xA42E1E, 0xA55834, 0xA6824A, 0xA7AC60, 0xA8D676, 0xAA008C, 0xAB2AA2, 0xAC54B8, 0xAD7ECE, 0xAEA8E4, 0xAFD2FA, 0xB0FD10, 0xB22726, 0xB3513C, 0xB47B52, 0xB5A568, 0xB6CF7E, 0xB7F994, 0xB923AA, 0xBA4DC0, 0xBB77D6, 0xBCA1EC, 0xBDCC02, 0xBEF618, 0xC0202E, 0xC14A44, 0xC2745A, 0xC39E70, 0xC4C886, 0xC5F29C, 0xC71CB2, 0xC846C8, 0xC970DE, 0xCA9AF4, 0xCBC50A, 0xCCEF20, 0xCE1936, 0xCF434C, 0xD06D62, 0xD19778, 0xD2C18E, 0xD3EBA4, 0xD515BA, 0xD63FD0, 0xD769E6, 0xD893FC, 0xD9BE12, 0xDAE828, 0xDC123E, 0xDD3C54, 0xDE666A, 0xDF9080, 0xE0BA96, 0xE1E4AC, 0xE30EC2, 0xE438D8, 0xE562EE, 0xE68D04, 0xE7B71A, 0xE8E130, 0xEA0B46, 0xEB355C, 0xEC5F72, 0xED8988, 0xEEB39E, 0xEFDDB4, 0xF107CA, 0xF231E0, 0xF35BF6, 0xF4860C, 0xF5B022, 0xF6DA38, 0xF8044E, 0xF92E64, 0xFA587A, 0xFB8290, 0xFCACA6, 0xFDD6BC, 0xFF00D2, 0x1002AE8, 0x10154FE, 0x1027F14, 0x103A92A, 0x104D340, 0x105FD56, 0x107276C, 0x1085182, 0x1097B98, 0x10AA5AE, 0x10BCFC4, 0x10CF9DA, 0x10E23F0, 0x10F4E06, 0x110781C, 0x111A232, 0x112CC48, 0x113F65E, 0x1152074, 0x1164A8A
};
static const int yuv2rgbtable_ru[256] = {
0xFEFDA500, 0xFEFFA9B6, 0xFF01AE6C, 0xFF03B322, 0xFF05B7D8, 0xFF07BC8E, 0xFF09C144, 0xFF0BC5FA, 0xFF0DCAB0, 0xFF0FCF66, 0xFF11D41C, 0xFF13D8D2, 0xFF15DD88, 0xFF17E23E, 0xFF19E6F4, 0xFF1BEBAA, 0xFF1DF060, 0xFF1FF516, 0xFF21F9CC, 0xFF23FE82, 0xFF260338, 0xFF2807EE, 0xFF2A0CA4, 0xFF2C115A, 0xFF2E1610, 0xFF301AC6, 0xFF321F7C, 0xFF342432, 0xFF3628E8, 0xFF382D9E, 0xFF3A3254, 0xFF3C370A, 0xFF3E3BC0, 0xFF404076, 0xFF42452C, 0xFF4449E2, 0xFF464E98, 0xFF48534E, 0xFF4A5804, 0xFF4C5CBA, 0xFF4E6170, 0xFF506626, 0xFF526ADC, 0xFF546F92, 0xFF567448, 0xFF5878FE, 0xFF5A7DB4, 0xFF5C826A, 0xFF5E8720, 0xFF608BD6, 0xFF62908C, 0xFF649542, 0xFF6699F8, 0xFF689EAE, 0xFF6AA364, 0xFF6CA81A, 0xFF6EACD0, 0xFF70B186, 0xFF72B63C, 0xFF74BAF2, 0xFF76BFA8, 0xFF78C45E, 0xFF7AC914, 0xFF7CCDCA, 0xFF7ED280, 0xFF80D736, 0xFF82DBEC, 0xFF84E0A2, 0xFF86E558, 0xFF88EA0E, 0xFF8AEEC4, 0xFF8CF37A, 0xFF8EF830, 0xFF90FCE6, 0xFF93019C, 0xFF950652, 0xFF970B08, 0xFF990FBE, 0xFF9B1474, 0xFF9D192A, 0xFF9F1DE0, 0xFFA12296, 0xFFA3274C, 0xFFA52C02, 0xFFA730B8, 0xFFA9356E, 0xFFAB3A24, 0xFFAD3EDA, 0xFFAF4390, 0xFFB14846, 0xFFB34CFC, 0xFFB551B2, 0xFFB75668, 0xFFB95B1E, 0xFFBB5FD4, 0xFFBD648A, 0xFFBF6940, 0xFFC16DF6, 0xFFC372AC, 0xFFC57762, 0xFFC77C18, 0xFFC980CE, 0xFFCB8584, 0xFFCD8A3A, 0xFFCF8EF0, 0xFFD193A6, 0xFFD3985C, 0xFFD59D12, 0xFFD7A1C8, 0xFFD9A67E, 0xFFDBAB34, 0xFFDDAFEA, 0xFFDFB4A0, 0xFFE1B956, 0xFFE3BE0C, 0xFFE5C2C2, 0xFFE7C778, 0xFFE9CC2E, 0xFFEBD0E4, 0xFFEDD59A, 0xFFEFDA50, 0xFFEFDDB4, 0xF107CA, 0xF231E0, 0xF35BF6, 0xF4860C, 0xF5B022, 0xF6DA38, 0xF8044E, 0xF92E64, 0xFA587A, 0xFB8290, 0xFCACA6, 0xFDD6BC, 0xFF00D2, 0x1002AE8, 0x10154FE, 0x1027F14, 0x103A92A, 0x104D340, 0x105FD56, 0x107276C, 0x1085182, 0x1097B98, 0x10AA5AE, 0x10BCFC4, 0x10CF9DA, 0x10E23F0, 0x10F4E06, 0x110781C, 0x111A232, 0x112CC48, 0x113F65E, 0x1152074, 0x1164A8A
};
static const int yuv2rgbtable_gu[256] = {
0xFFCDD300, 0xFFCE375A, 0xFFCE9BB4, 0xFFCF000E, 0xFFCF6468, 0xFFCFC8C2, 0xFFD02D1C, 0xFFD09176, 0xFFD0F5D0, 0xFFD15A2A, 0xFFD1BE84, 0xFF222DE, 0xFF28738, 0xFF2EB92, 0xFF34FEC, 0xFF3B446, 0xFF418A0, 0xFF47CFA, 0xFF4E154, 0xFF545AE, 0xFF5AA08, 0xFF60E62, 0xFF672BC, 0xFF6D716, 0xFF73B70, 0xFF79FCA, 0xFF80424, 0xFF8687E, 0xFF8CCD8, 0xFF93132, 0xFF9958C, 0xFF9F9E6, 0xFA5E40, 0xFAC29A, 0xFB26F4, 0xFB8B4E, 0xFBEFA8, 0xFC5402, 0xFCB85C, 0xFD1CB6, 0xDD8110, 0xDDE56A, 0xDE49C4, 0xDEAE1E, 0xDF1278, 0xDF76D2, 0xDFDB2C, 0xE03F86, 0xE0A3E0, 0xE1083A, 0xE16C94, 0xE1D0EE, 0xE23548, 0xE299A2, 0xE2FDFC, 0xE36256, 0xE3C6B0, 0xE42B0A, 0xE48F64, 0xE4F3BE, 0xE55818, 0xE5BC72, 0xE620CC, 0xE68526, 0xE6E980, 0xE74DDA, 0xE7B234, 0xE8168E, 0xE87AE8, 0xE8DF42, 0xE9439C, 0xE9A7F6, 0xEA0C50, 0xEA70AA, 0xEAD504, 0xEB395E, 0xEB9DB8, 0xEC0212, 0xEC666C, 0xECCAC6, 0xED2F20, 0xED937A, 0xEDF7D4, 0xEE5C2E, 0xEEC088, 0xEF24E2, 0xEF893C, 0xFFED96, 0xF051F0, 0xF0B64A, 0xF11AA4, 0xF17EFE, 0xF1E358, 0xF247B2, 0xF2AC0C, 0xF31066, 0xF374C0, 0xF3D91A, 0xF43D74, 0xF4A1CE, 0xF50628, 0xF56A82, 0xF5CEDC, 0xF63336, 0xF69790, 0xF6FBEA, 0xF76044, 0xF7C49E, 0xF828F8, 0xF88D52, 0xF8F1AC, 0xF95606, 0xF9BA60, 0xFA1EBA, 0xFA8314, 0xFAE76E, 0xFB4BC8, 0xFBB022, 0xFC147C, 0xFC78D6, 0xFCDD30, 0xFD418A, 0xFDA5E4, 0xFE0A3E, 0xFE6E98, 0xFED2F2, 0xFFFF374C, 0xFFFF9BA6, 0x0, 0x645A, 0xC8B4, 0x12D0E, 0x19168, 0x1F5C2, 0x25A1C, 0x2BE76, 0x322D0, 0x3872A, 0x3EB84, 0x44FDE, 0x4B438, 0x51892, 0x57CEC, 0x5E146, 0x645A0, 0x6A9FA, 0x70E54, 0x772AE, 0x7D708, 0x83B62, 0x89FBC, 0x90416, 0x96870, 0x9CCCA, 0xA3124, 0xA957E, 0xAF9D8, 0xB5E32, 0xBC28C, 0xC26E6, 0xC8B40, 0xCEF9A, 0xD53F4, 0xDB84E, 0xE1CA8, 0xE8102, 0xEE55C, 0xF49B6, 0xFAE10, 0x10126A, 0x1076C4, 0x10DB1E, 0x113F78, 0x11A3D2, 0x12082C, 0x126C86, 0x12D0E0, 0x13353A, 0x139994, 0x13FDEE, 0x146248, 0x14C6A2, 0x152AFC, 0x158F56, 0x15F3B0, 0x16580A, 0x16BC64, 0x1720BE, 0x178518, 0x17E972, 0x184DCC, 0x18B226, 0x191680, 0x197ADA, 0x19DF34, 0x1A438E, 0x1AA7E8, 0x1B0C42, 0x1B709C, 0x1BD4F6, 0x1C3950, 0x1C9DAA, 0x1D0204, 0x1D665E, 0x1DCAB8, 0x1E2F12, 0x1E936C, 0x1EF7C6, 0x1F5C20, 0x1FC07A, 0x2024D4, 0x20892E, 0x20ED88, 0x2151E2, 0x21B63C, 0x221A96, 0x227EF0, 0x22E34A, 0x2347A4, 0x23ABFE, 0x241058, 0x2474B2, 0x24D90C, 0x253D66, 0x25A1C0, 0x26061A, 0x266A74, 0x26CECE, 0x273328, 0x279782, 0x27FBDC, 0x286036, 0x28C490, 0x2928EA, 0x298D44, 0x29F19E, 0x2A55F8, 0x2ABA52, 0x2B1EAC, 0x2B8306, 0x2BE760, 0x2C4BBA, 0x2CB014, 0x2D146E, 0x2D78C8, 0x2DDD22, 0x2E417C, 0x2EA5D6, 0x2F0A30, 0x2F6E8A, 0x2FD2E4, 0x30373E, 0x309B98, 0x30FFF2, 0x31644C, 0x31C8A6
};
static const int yuv2rgbtable_gv[256] = {
0xFF97E900, 0xFF98B92E, 0xFF99895C, 0xFF9A598A, 0xFF9B29B8, 0xFF9BF9E6, 0xFF9CCA14, 0xFF9D9A42, 0xFF9E6A70, 0xFF9F3A9E, 0xFFA00ACC, 0xFFA0DAFA, 0xFFA1AB28, 0xFFA27B56, 0xFFA34B84, 0xFFA41BB2, 0xFFA4EBE0, 0xFFA5BC0E, 0xFFA68C3C, 0xFFA75C6A, 0xFFA82C98, 0xFFA8FCC6, 0xFFA9CCF4, 0xFFAA9D22, 0xFFAB6D50, 0xFFAC3D7E, 0xFFAD0DAC, 0xFFADDDDA, 0xFFAEAE08, 0xFFAF7E36, 0xFFB04E64, 0xFFB11E92, 0xFFB1EEC0, 0xFFB2BEEE, 0xFFB38F1C, 0xFFB45F4A, 0xFFB52F78, 0xFFB5FFA6, 0xFFB6CFD4, 0xFFB7A002, 0xFFB87030, 0xFFB9405E, 0xFFBA108C, 0xFFBAE0BA, 0xFFBBB0E8, 0xFFBC8116, 0xFFBD5144, 0xFFBE2172, 0xFFBEF1A0, 0xFFBFC1CE, 0xFFC091FC, 0xFFC1622A, 0xFFC23258, 0xFFC30286, 0xFFC3D2B4, 0xFFC4A2E2, 0xFFC57310, 0xFFC6433E, 0xFFC7136C, 0xFFC7E39A, 0xFFC8B3C8, 0xFFC983F6, 0xFFCA5424, 0xFFCB2452, 0xFFCBF480, 0xFFCCC4AE, 0xFFCD94DC, 0xFFCE650A, 0xFFCF3538, 0xFFD00566, 0xFFD0D594, 0xFFD1A5C2, 0xFFD275F0, 0xFFD3461E, 0xFFD4164C, 0xFFD4E67A, 0xFFD5B6A8, 0xFFD686D6, 0xFFD75704, 0xFFD82732, 0xFFD8F760, 0xFFD9C78E, 0xFFDA97BC, 0xFFDB67EA, 0xFFDC3818, 0xFFDD0846, 0xFFDDD874, 0x00DEA8A2, 0xDF78D0, 0xE048FE, 0xE1192C, 0xE1E95A, 0xE2B988, 0xE389B6, 0xE459E4, 0xE52A12, 0xE5FA40, 0xE6CA6E, 0xE79A9C, 0xE86ACA, 0xE93AF8, 0xEA0B26, 0xEADB54, 0xEBAB82, 0xEC7BB0, 0xED4BDE, 0xEE1C0C, 0xEEEC3A, 0xEFBC68, 0xF08C96, 0xF15CC4, 0xF22CF2, 0xF2FD20, 0xF3CD4E, 0xF49D7C, 0xF56DAA, 0xF63DD8, 0xF70E06, 0xF7DE34, 0xF8AE62, 0xF97E90, 0xFA4EBE, 0xFB1EEC, 0xFBEF1A, 0xFCBF48, 0xFD8F76, 0xFE5FA4, 0xFFFF2FD2, 0x0, 0xD02E, 0x1A05C, 0x2708A, 0x340B8, 0x410E6, 0x4E114, 0x5B142, 0x68170, 0x7519E, 0x821CC, 0x8F1FA, 0x9C228, 0xA9256, 0xB6284, 0xC32B2, 0xD02E0, 0xDD30E, 0xEA33C, 0xF736A, 0x104398, 0x1113C6, 0x11E3F4, 0x12B422, 0x138450, 0x14547E, 0x1524AC, 0x15F4DA, 0x16C508, 0x179536, 0x186564, 0x193592, 0x1A05C0, 0x1AD5EE, 0x1BA61C, 0x1C764A, 0x1D4678, 0x1E16A6, 0x1EE6D4, 0x1FB702, 0x208730, 0x21575E, 0x22278C, 0x22F7BA, 0x23C7E8, 0x249816, 0x256844, 0x263872, 0x2708A0, 0x27D8CE, 0x28A8FC, 0x29792A, 0x2A4958, 0x2B1986, 0x2BE9B4, 0x2CB9E2, 0x2D8A10, 0x2E5A3E, 0x2F2A6C, 0x2FFA9A, 0x30CAC8, 0x319AF6, 0x326B24, 0x333B52, 0x340B80, 0x34DBAE, 0x35ABDC, 0x367C0A, 0x374C38, 0x381C66, 0x38EC94, 0x39BCC2, 0x3A8CF0, 0x3B5D1E, 0x3C2D4C, 0x3CFD7A, 0x3DCDA8, 0x3E9DD6, 0x3F6E04, 0x403E32, 0x410E60, 0x41DE8E, 0x42AEBC, 0x437EEA, 0x444F18, 0x451F46, 0x45EF74, 0x46BFA2, 0x478FD0, 0x485FFE, 0x49302C, 0x4A005A, 0x4AD088, 0x4BA0B6, 0x4C70E4, 0x4D4112, 0x4E1140, 0x4EE16E, 0x4FB19C, 0x5081CA, 0x5151F8, 0x522226, 0x52F254, 0x53C282, 0x5492B0, 0x5562DE, 0x56330C, 0x57033A, 0x57D368, 0x58A396, 0x5973C4, 0x5A43F2, 0x5B1420, 0x5BE44E, 0x5CB47C, 0x5D84AA, 0x5E54D8, 0x5F2506, 0x5FF534, 0x60C562, 0x619590, 0x6265BE, 0x6335EC, 0x64061A, 0x64D648, 0x65A676, 0x6676A4, 0x6746D2
};

static const int yuv2rgbtable_bv[256] = {
0xFF33A280, 0xFF353B3B, 0xFF36D3F6, 0xFF386CB1, 0xFF3A056C, 0xFF3B9E27, 0xFF3D36E2, 0xFF3ECF9D, 0xFF406858, 0xFF420113, 0xFF4399CE, 0xFF453289, 0xFF46CB44, 0xFF4863FF, 0xFF49FCBA, 0xFF4B9575, 0xFF4D2E30, 0xFF4EC6EB, 0xFF505FA6, 0xFF51F861, 0xFF53911C, 0xFF5529D7, 0xFF56C292, 0xFF585B4D, 0xFF59F408, 0xFF5B8CC3, 0xFF5D257E, 0xFF5EBE39, 0xFF6056F4, 0xFF61EFAF, 0xFF63886A, 0xFF652125, 0xFF66B9E0, 0xFF68529B, 0xFF69EB56, 0xFF6B8411, 0xFF6D1CCC, 0xFF6EB587, 0xFF704E42, 0xFF71E6FD, 0xFF737FB8, 0xFF751873, 0xFF76B12E, 0xFF7849E9, 0xFF79E2A4, 0xFF7B7B5F, 0xFF7D141A, 0xFF7EACD5, 0xFF804590, 0xFF81DE4B, 0xFF837706, 0xFF850FC1, 0xFF86A87C, 0xFF884137, 0xFF89D9F2, 0xFF8B72AD, 0xFF8D0B68, 0xFF8EA423, 0xFF903CDE, 0xFF91D599, 0xFF936E54, 0xFF95070F, 0xFF969FCA, 0xFF983885, 0xFF99D140, 0xFF9B69FB, 0xFF9D02B6, 0xFF9E9B71, 0xFFA0342C, 0xFFA1CCE7, 0xFFA365A2, 0xFFA4FE5D, 0xFFA69718, 0xFFA82FD3, 0xFFA9C88E, 0xFFAB6149, 0xFFACFA04, 0xFFAE92BF, 0xFFB02B7A, 0xFFB1C435, 0xFFB35CF0, 0xFFB4F5AB, 0xFFB68E66, 0xFFB82721, 0xFFB9BFDC, 0xFFBB5897, 0xFFBCF152, 0xFFBE8A0D, 0xFFC022C8, 0xFFC1BB83, 0xFFC3543E, 0xFFC4ECF9, 0xFFC685B4, 0xFFC81E6F, 0xFFC9B72A, 0xFFCB4FE5, 0xFFCCE8A0, 0xFFCE815B, 0xFFD01A16, 0xFFD1B2D1, 0xFFD34B8C, 0xFFD4E447, 0xFFD67D02, 0xFFD815BD, 0xFFD9AE78, 0xFFDB4733, 0xFFDCDFEE, 0xFFDE78A9, 0xFFE01164, 0xFFE1AA1F, 0xFFE342DA, 0xFFE4DB95, 0xFFE67450, 0xFFE80D0B, 0xFFE9A5C6, 0xFFEB3E81, 0xFFECD73C, 0xFFEE6FF7, 0xFFF008B2, 0xFFF1A16D, 0xFFF33A28, 0xFFF4D2E3, 0xFFF66B9E, 0xFFF80459, 0xFFF99D14, 0xFFFB35CF, 0xFFFCCE8A, 0xFFFE6745, 0x0, 0x198BB, 0x33176, 0x4CA31, 0x662EC, 0x7FBA7, 0x99462, 0xB2D1D, 0xCC5D8, 0xE5E93, 0xFF74E, 0x119009, 0x1328C4, 0x14C17F, 0x165A3A, 0x17F2F5, 0x198BB0, 0x1B246B, 0x1CBD26, 0x1E55E1, 0x1FEE9C, 0x218757, 0x232012, 0x24B8CD, 0x265188, 0x27EA43, 0x2982FE, 0x2B1BB9, 0x2CB474, 0x2E4D2F, 0x2FE5EA, 0x317EA5, 0x331760, 0x34B01B, 0x3648D6, 0x37E191, 0x397A4C, 0x3B1307, 0x3CABC2, 0x3E447D, 0x3FDD38, 0x4175F3, 0x430EAE, 0x44A769, 0x464024, 0x47D8DF, 0x49719A, 0x4B0A55, 0x4CA310, 0x4E3BCB, 0x4FD486, 0x516D41, 0x5305FC, 0x549EB7, 0x563772, 0x57D02D, 0x5968E8, 0x5B01A3, 0x5C9A5E, 0x5E3319, 0x5FCBD4, 0x61648F, 0x62FD4A, 0x649605, 0x662EC0, 0x67C77B, 0x696036, 0x6AF8F1, 0x6C91AC, 0x6E2A67, 0x6FC322, 0x715BDD, 0x72F498, 0x748D53, 0x76260E, 0x77BEC9, 0x795784, 0x7AF03F, 0x7C88FA, 0x7E21B5, 0x7FBA70, 0x81532B, 0x82EBE6, 0x8484A1, 0x861D5C, 0x87B617, 0x894ED2, 0x8AE78D, 0x8C8048, 0x8E1903, 0x8FB1BE, 0x914A79, 0x92E334, 0x947BEF, 0x9614AA, 0x97AD65, 0x994620, 0x9ADEDB, 0x9C7796, 0x9E1051, 0x9FA90C, 0xA141C7, 0xA2DA82, 0xA4733D, 0xA60BF8, 0xA7A4B3, 0xA93D6E, 0xAAD629, 0xAC6EE4, 0xAE079F, 0xAFA05A, 0xB13915, 0xB2D1D0, 0xB46A8B, 0xB60346, 0xB79C01, 0xB934BC, 0xBACD77, 0xBC6632, 0xBDFEED, 0xBF97A8, 0xC13063, 0xC2C91E, 0xC461D9, 0xC5FA94, 0xC7934F, 0xC92C0A, 0xCAC4C5
};

static enum {UNKNOWN, BRCM_GENERIC, XILLEON, VULCAN, PALLAS} stb_type = UNKNOWN;
static int chr_luma_stride = 0x40;
static int chr_luma_register_offset = 0;
static off_t registeroffset = 0;

void getvideo(unsigned char *video, int *xres, int *yres);
void getosd(unsigned char *osd, int *xres, int *yres);
void fast_resize(const unsigned char *source, unsigned char *dest, int xsource, int ysource, int xdest, int ydest, int colors);
void combine(unsigned char *output, const unsigned char *video, const unsigned char *osd, int vleft, int vtop, int vwidth, int vheight, int xres, int yres);

int main(int argc, char **argv)
{
	int xres_v = 0, yres_v = 0, xres_o = 0, yres_o = 0, xres = 0, yres = 0;
	int c, use_png = 0, use_jpg = 0, jpg_quality = 80;
	int quiet = 0, to_stdout = 0, req_width = 0, req_height = 0;
	int video_only = 0, osd_only = 0;
	unsigned char *video = NULL, *osd = NULL, *output = NULL;
	const char* filename = "/tmp/screenshot.bmp";
	char buf[256];

	FILE *fp = fopen("/proc/fb", "r");
	if (fp) {
		while (fgets(buf, sizeof(buf), fp)) {
			if (strcasestr(buf, "VULCAN")) stb_type = VULCAN;
			if (strcasestr(buf, "PALLAS")) stb_type = PALLAS;
			if (strcasestr(buf, "XILLEON")) stb_type = XILLEON;
		}
		fclose(fp);
	}

	if (stb_type == UNKNOWN) {
		stb_type = BRCM_GENERIC;
		registeroffset = 0x10600000;
		chr_luma_stride = 0x40;
		chr_luma_register_offset = 0x34;
	}

	while ((c = getopt(argc, argv, "hj:pqsvor:")) != -1) {
		switch (c) {
			case 'p':
				use_png = 1;
				filename = "/tmp/screenshot.png";
				break;
			case 'j':
				use_jpg = 1;
				jpg_quality = atoi(optarg);
				filename = "/tmp/screenshot.jpg";
				break;
			case 'q':
				quiet = 1;
				break;
			case 's':
				to_stdout = 1;
				filename = NULL;
				break;
			case 'v':
				video_only = 1;
				osd_only = 0;
				break;
			case 'o':
				osd_only = 1;
				video_only = 0;
				break;
			case 'r':
				if (sscanf(optarg, "%d:%d", &req_width, &req_height) != 2) {
					req_width = atoi(optarg);
					req_height = 0;
				}
				break;
			case 'h':
			default:
				fprintf(stderr, "Usage: grab [-p] [-j quality] [-q] [-s] [-v] [-o] [-r width:height] [filename]\n");
				return 1;
		}
	}
	if (optind < argc && !to_stdout)
		filename = argv[optind];

	// Puffer-Größe sicherheitshalber auf UHD maximieren, um Overflows beim Einlesen zu verhindern
	size_t mallocsize = 3840U * 2160U;
	output = (unsigned char *)malloc(mallocsize * 3U);
	if (!output) {
		if (!quiet) fprintf(stderr, "Out of memory.\n");
		return 1;
	}

	if (!video_only) {
		osd = (unsigned char *)malloc(mallocsize * 4U);
		if (osd) getosd(osd, &xres_o, &yres_o);
	}

	if (!osd_only) {
		video = (unsigned char *)malloc(mallocsize * 3U);
		if (video) getvideo(video, &xres_v, &yres_v);
	}

	if (osd_only || (xres_v <= 0 || yres_v <= 0)) {
		if (xres_o <= 0 || yres_o <= 0) {
			if (!quiet) fprintf(stderr, "OSD Capture failed.\n");
			goto error_cleanup;
		}
		xres = xres_o; yres = yres_o;
		for(int i = 0; i < xres * yres; ++i) {
			output[i*3+0] = osd[i*4+0];
			output[i*3+1] = osd[i*4+1];
			output[i*3+2] = osd[i*4+2];
		}
	} else if (video_only || (xres_o <= 0 || yres_o <= 0)) {
		if (xres_v <= 0 || yres_v <= 0) {
			if (!quiet) fprintf(stderr, "Video Capture failed.\n");
			goto error_cleanup;
		}
		xres = xres_v; yres = yres_v;
		memcpy(output, video, xres * yres * 3);
	} else {
		xres = xres_o; yres = yres_o;
		if (xres_v != xres || yres_v != yres) {
			unsigned char *resized_video = (unsigned char *)malloc(xres * yres * 3);
			if (resized_video) {
				fast_resize(video, resized_video, xres_v, yres_v, xres, yres, 3);
				combine(output, resized_video, osd, 0, 0, xres, yres, xres, yres);
				free(resized_video);
			}
		} else {
			combine(output, video, osd, 0, 0, xres, yres, xres, yres);
		}
	}

	if (req_width > 0) {
		if (req_height <= 0) {
			req_height = (yres * req_width) / xres;
		}
		unsigned char *scaled_output = (unsigned char *)malloc(req_width * req_height * 3);
		if (scaled_output) {
			fast_resize(output, scaled_output, xres, yres, req_width, req_height, 3);
			free(output);
			output = scaled_output;
			xres = req_width;
			yres = req_height;
		}
	}

	FILE *fd2 = to_stdout ? stdout : fopen(filename, "wb");
	if (!fd2) {
		if (!quiet) fprintf(stderr, "Failed to open output stream.\n");
		goto error_cleanup;
	}

	if (!use_png && !use_jpg) {
		// FEHLER BEHOBEN: Array-Größe [54] für den BMP Header korrigiert
		unsigned char hdr[54];
		memset(hdr, 0, 54);
		hdr[0] = 'B'; hdr[1] = 'M';
		uint32_t file_size = (xres * yres * 3) + 54;
		memcpy(&hdr[2], &file_size, 4);
		hdr[10] = 54; hdr[14] = 40;
		memcpy(&hdr[18], &xres, 4);
		memcpy(&hdr[22], &yres, 4);
		hdr[26] = 1; hdr[28] = 24;
		fwrite(hdr, 1, 54, fd2);

		for (int y = yres - 1; y >= 0; y--)
			fwrite(output + (y * xres * 3), xres * 3, 1, fd2);
	} 
	else if (use_png) {
		png_structp png_ptr = png_create_write_struct(PNG_LIBPNG_VER_STRING, NULL, NULL, NULL);
		png_infop info_ptr = png_create_info_struct(png_ptr);
		png_init_io(png_ptr, fd2);
		png_set_bgr(png_ptr);
		png_set_IHDR(png_ptr, info_ptr, xres, yres, 8, PNG_COLOR_TYPE_RGB, PNG_INTERLACE_NONE, PNG_COMPRESSION_TYPE_BASE, PNG_FILTER_TYPE_BASE);
		png_write_info(png_ptr, info_ptr);
		png_bytep *row_pointers = (png_bytep*)malloc(sizeof(png_bytep) * yres);
		for (int y = 0; y < yres; y++) row_pointers[y] = output + (y * xres * 3);
		png_write_image(png_ptr, row_pointers);
		png_write_end(png_ptr, info_ptr);
		png_destroy_write_struct(&png_ptr, &info_ptr);
		free(row_pointers);
	} 
	else if (use_jpg) {
		struct jpeg_compress_struct cinfo;
		struct jpeg_error_mgr jerr;
		cinfo.err = jpeg_std_error(&jerr);
		jpeg_create_compress(&cinfo);
		jpeg_stdio_dest(&cinfo, fd2);
		cinfo.image_width = xres; cinfo.image_height = yres;
		cinfo.input_components = 3; cinfo.in_color_space = JCS_RGB;
		jpeg_set_defaults(&cinfo);
		jpeg_set_quality(&cinfo, jpg_quality, TRUE);
		jpeg_start_compress(&cinfo, TRUE);
		
		for (int i = 0; i < xres * yres; ++i) {
			SWAP(output[i*3+0], output[i*3+2]);
		}
		
		while (cinfo.next_scanline < cinfo.image_height) {
			JSAMPROW row_pointer = &output[cinfo.next_scanline * xres * 3];
			jpeg_write_scanlines(&cinfo, &row_pointer, 1);
		}
		jpeg_finish_compress(&cinfo);
		jpeg_destroy_compress(&cinfo);
	}

	if (!to_stdout) fclose(fd2);
	if (video) free(video);
	if (osd) free(osd);
	if (output) free(output);
	return 0;

error_cleanup:
	if (video) free(video);
	if (osd) free(osd);
	if (output) free(output);
	return 1;
}


// Funktions-Prototypen für die HiSilicon System- und Multimedia-APIs
typedef int            HI_S32;
typedef unsigned int   HI_U32;
typedef void           HI_VOID;
typedef HI_U32         HI_HANDLE;

typedef struct {
	HI_U32  u32Unk0;
	HI_U32  u32YPhyAddr;    // Physikalische Adresse Luminanz
	HI_U32  u32CPhyAddr;    // Physikalische Adresse Chrominanz
	HI_U32  u32Unk0c;
	HI_U32  u32YStride;
	HI_U32  u32CStride;
	HI_U32  u32Pad1[7];
	HI_U32  u32Width;
	HI_U32  u32Height;
	HI_U32  u32Pad2[7];
	HI_U32  u32PixelFormat;
	HI_U32  u32Pad3[64];
} HI_UNF_VIDEO_FRAME_INFO_S;

typedef HI_S32 (*PFN_HI_SYS_Init)(HI_VOID);
typedef HI_S32 (*PFN_HI_SYS_DeInit)(HI_VOID);
typedef HI_S32 (*PFN_HI_UNF_DISP_Init)(HI_VOID);
typedef HI_S32 (*PFN_HI_UNF_DISP_DeInit)(HI_VOID);
typedef HI_S32 (*PFN_HI_UNF_DISP_Open)(HI_U32 enDisp);
typedef HI_S32 (*PFN_HI_UNF_DISP_AcquireSnapshot)(HI_U32 enDisp, HI_UNF_VIDEO_FRAME_INFO_S *pstSnapShot);
typedef HI_S32 (*PFN_HI_UNF_DISP_ReleaseSnapshot)(HI_U32 enDisp, const HI_UNF_VIDEO_FRAME_INFO_S *pstSnapShot);
typedef void*  (*PFN_HI_MMZ_Map)(HI_U32 u32PhyAddr, HI_U32 u32Cached);
typedef HI_S32 (*PFN_HI_MMZ_Unmap)(HI_U32 u32PhyAddr);

void getvideo(unsigned char *video, int *xres, int *yres)
{
	*xres = 0;
	*yres = 0;

	// Dynamisches Laden der HiSilicon SoC Treiber-Bibliotheken
	void *hisi_lib_common = dlopen("/usr/lib/libhi_common.so", RTLD_LAZY | RTLD_GLOBAL);
	void *hisi_lib_msp    = dlopen("/usr/lib/libhi_msp.so", RTLD_LAZY | RTLD_GLOBAL);

	if (!hisi_lib_common || !hisi_lib_msp) {
		// Fallback auf Standard Broadcom-Einlesung, falls keine HiSilicon-Treiber geladen sind
		stb_type = BRCM_GENERIC;
		return;
	}

	PFN_HI_SYS_Init pfnSysInit = (PFN_HI_SYS_Init)dlsym(hisi_lib_common, "HI_SYS_Init");
	PFN_HI_SYS_DeInit pfnSysDeInit = (PFN_HI_SYS_DeInit)dlsym(hisi_lib_common, "HI_SYS_DeInit");
	PFN_HI_UNF_DISP_Init pfnDispInit = (PFN_HI_UNF_DISP_Init)dlsym(hisi_lib_msp, "HI_UNF_DISP_Init");
	PFN_HI_UNF_DISP_DeInit pfnDispDeInit = (PFN_HI_UNF_DISP_DeInit)dlsym(hisi_lib_msp, "HI_UNF_DISP_DeInit");
	PFN_HI_UNF_DISP_Open pfnDispOpen = (PFN_HI_UNF_DISP_Open)dlsym(hisi_lib_msp, "HI_UNF_DISP_Open");
	PFN_HI_UNF_DISP_AcquireSnapshot pfnAcquire = (PFN_HI_UNF_DISP_AcquireSnapshot)dlsym(hisi_lib_msp, "HI_UNF_DISP_AcquireSnapshot");
	PFN_HI_UNF_DISP_ReleaseSnapshot pfnRelease = (PFN_HI_UNF_DISP_ReleaseSnapshot)dlsym(hisi_lib_msp, "HI_UNF_DISP_ReleaseSnapshot");
	
	// MMZ (Memory Management Zone) Mapping-Funktionen auflösen
	PFN_HI_MMZ_Map pfnMMZMap = (PFN_HI_MMZ_Map)dlsym(hisi_lib_common, "HI_MMZ_Map");
	if (!pfnMMZMap) pfnMMZMap = (PFN_HI_MMZ_Map)dlsym(hisi_lib_msp, "HI_MMZ_Map");
	PFN_HI_MMZ_Unmap pfnMMZUnmap = (PFN_HI_MMZ_Unmap)dlsym(hisi_lib_common, "HI_MMZ_Unmap");
	if (!pfnMMZUnmap) pfnMMZUnmap = (PFN_HI_MMZ_Unmap)dlsym(hisi_lib_msp, "HI_MMZ_Unmap");

	if (!pfnSysInit || !pfnDispInit || !pfnAcquire || !pfnRelease || !pfnMMZMap || !pfnMMZUnmap) {
		if (hisi_lib_msp) dlclose(hisi_lib_msp);
		if (hisi_lib_common) dlclose(hisi_lib_common);
		return;
	}

	// Multimedia Hardware-Subsystem initialisieren
	if (pfnSysInit() != 0) goto close_libs;
	if (pfnDispInit() != 0) { pfnSysDeInit(); goto close_libs; }
	if (pfnDispOpen != NULL) pfnDispOpen(1); // DISP1 öffnen

	HI_UNF_VIDEO_FRAME_INFO_S *pFrame = (HI_UNF_VIDEO_FRAME_INFO_S*)calloc(1, 4096);
	if (!pFrame) goto deinit_disp;

	// Direkten Video-Snapshot aus der Hardware-Engine sichern (Verhindert grüne Pixelsalat-Ausgabe)
	if (pfnAcquire(1, pFrame) != 0) {
		free(pFrame);
		goto deinit_disp;
	}

	if (pFrame->u32Width && pFrame->u32Height && pFrame->u32YPhyAddr) {
		unsigned char *y_virt = (unsigned char*)pfnMMZMap(pFrame->u32YPhyAddr, 0);
		unsigned char *uv_virt = NULL;
		int mapped_separately = 0;

		if (y_virt) {
			int w = (int)pFrame->u32Width;
			int h = (int)pFrame->u32Height;
			int ystride = (int)pFrame->u32YStride;
			int cstride = (int)pFrame->u32CStride ? (int)pFrame->u32CStride : ystride;

			// Prüfen, ob Chrominanz im selben Speicherbereich liegt
			if (pFrame->u32CPhyAddr > pFrame->u32YPhyAddr && (pFrame->u32CPhyAddr - pFrame->u32YPhyAddr) < (64U * 1024U * 1024U)) {
				uv_virt = y_virt + (pFrame->u32CPhyAddr - pFrame->u32YPhyAddr);
			} else if (pFrame->u32CPhyAddr) {
				uv_virt = (unsigned char*)pfnMMZMap(pFrame->u32CPhyAddr, 0);
				mapped_separately = 1;
			}

			if (uv_virt) {
				// Native NV21/YUV-Farbkonvertierung zu internem BGR (Löst das Farbproblem)
				for (int i = 0; i < h; i++) {
					for (int j = 0; j < w; j++) {
						int y_val = y_virt[i * ystride + j] - 16;
						int v_val = uv_virt[(i / 2) * cstride + (j & ~1)] - 128;
						int u_val = uv_virt[(i / 2) * cstride + (j & ~1) + 1] - 128;

						int r = CLAMP((298 * y_val + 409 * v_val + 128) >> 8);
						int g = CLAMP((298 * y_val - 100 * u_val - 208 * v_val + 128) >> 8);
						int b = CLAMP((298 * y_val + 516 * u_val + 128) >> 8);

						int off = (i * w + j) * 3;
						video[off + 0] = (unsigned char)b;
						video[off + 1] = (unsigned char)g;
						video[off + 2] = (unsigned char)r;
					}
				}
				*xres = w;
				*yres = h;
			}

			if (mapped_separately && pFrame->u32CPhyAddr) pfnMMZUnmap(pFrame->u32CPhyAddr);
			pfnMMZUnmap(pFrame->u32YPhyAddr);
		}
	}

	pfnRelease(1, pFrame);
	free(pFrame);

deinit_disp:
	if (pfnDispDeInit) pfnDispDeInit();
	if (pfnSysDeInit) pfnSysDeInit();

close_libs:
	if (hisi_lib_msp) dlclose(hisi_lib_msp);
	if (hisi_lib_common) dlclose(hisi_lib_common);
}



void getosd(unsigned char *osd, int *xres, int *yres)
{
	struct fb_fix_screeninfo fix_screeninfo;
	struct fb_var_screeninfo var_screeninfo;

	int fb = open("/dev/fb0", O_RDONLY);
	if (fb == -1) return;

	if (ioctl(fb, FBIOGET_FSCREENINFO, &fix_screeninfo) == -1 ||
		ioctl(fb, FBIOGET_VSCREENINFO, &var_screeninfo) == -1) {
		close(fb); return;
	}

	unsigned char *lfb = (unsigned char*)mmap(0, fix_screeninfo.smem_len, PROT_READ, MAP_SHARED, fb, 0);
	if (lfb == MAP_FAILED) { close(fb); return; }

	if (var_screeninfo.bits_per_pixel == 32) {
		for (unsigned int y = 0; y < var_screeninfo.yres; y++) {
			memcpy(osd + (y * var_screeninfo.xres * 4), lfb + (y * fix_screeninfo.line_length), var_screeninfo.xres * 4);
		}
		*xres = var_screeninfo.xres;
		*yres = var_screeninfo.yres;
	}
	munmap(lfb, fix_screeninfo.smem_len);
	close(fb);
}

void fast_resize(const unsigned char *source, unsigned char *dest, int xsource, int ysource, int xdest, int ydest, int colors)
{
	int x_ratio = (int)((xsource << 16) / xdest);
	int y_ratio = (int)((ysource << 16) / ydest);
	for (int i = 0; i < ydest; i++) {
		int y2_xsource = ((i * y_ratio) >> 16) * xsource;
		int i_xdest = i * xdest;
		for (int j = 0; j < xdest; j++) {
			int x2 = ((j * x_ratio) >> 16);
			int y2_x2_colors = (y2_xsource + x2) * colors;
			int i_x_colors = (i_xdest + j) * colors;
			for (int c = 0; c < colors; c++)
				dest[i_x_colors + c] = source[y2_x2_colors + c];
		}
	}
}

void combine(unsigned char *output, const unsigned char *video, const unsigned char *osd, int vleft, int vtop, int vwidth, int vheight, int xres, int yres)
{
	for (int y = 0; y < yres; y++) {
		int pos1 = y * xres * 4;
		int vpos1 = y * xres * 3;
		for (int x = 0; x < xres; x++) {
			int apos = pos1 + 3;
			int a2 = 0xFF - osd[apos];
			int pixel = (y * xres + x) * 3;
			output[vpos1++] = ((video[pixel + 0] * a2) + (osd[pos1++] * osd[apos])) >> 8;
			output[vpos1++] = ((video[pixel + 1] * a2) + (osd[pos1++] * osd[apos])) >> 8;
			output[vpos1++] = ((video[pixel + 2] * a2) + (osd[pos1++] * osd[apos])) >> 8;
			pos1++;
		}
	}
}



