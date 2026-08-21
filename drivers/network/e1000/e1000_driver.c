#include <rootos/rootdriver.h>

#define RX_COUNT 32u
#define TX_COUNT 8u
#define BUF_SIZE 2048u
#define REG_CTRL 0x0000u
#define REG_STATUS 0x0008u
#define REG_ICR 0x00C0u
#define REG_IMC 0x00D8u
#define REG_RCTL 0x0100u
#define REG_TCTL 0x0400u
#define REG_TIPG 0x0410u
#define REG_RDBAL 0x2800u
#define REG_RDBAH 0x2804u
#define REG_RDLEN 0x2808u
#define REG_RDH 0x2810u
#define REG_RDT 0x2818u
#define REG_TDBAL 0x3800u
#define REG_TDBAH 0x3804u
#define REG_TDLEN 0x3808u
#define REG_TDH 0x3810u
#define REG_TDT 0x3818u
#define REG_RAL0 0x5400u
#define REG_RAH0 0x5404u
#define CTRL_SLU (1u<<6)
#define RCTL_EN (1u<<1)
#define RCTL_BAM (1u<<15)
#define RCTL_SECRC (1u<<26)
#define TCTL_EN (1u<<1)
#define TCTL_PSP (1u<<3)
#define RX_DD 0x01u
#define RX_EOP 0x02u
#define TX_EOP 0x01u
#define TX_IFCS 0x02u
#define TX_RS 0x08u
#define TX_DD 0x01u

typedef struct __attribute__((packed)) { RootDrvU64 address; RootDrvU16 length, checksum; RootDrvU8 status, errors; RootDrvU16 special; } RxDesc;
typedef struct __attribute__((packed)) { RootDrvU64 address; RootDrvU16 length; RootDrvU8 cso, command, status, css; RootDrvU16 special; } TxDesc;
typedef struct { const RootDriverApi* api; RootDrvU64 mmio; RootDrvU8 mac[6]; RootDrvU32 rx_index, tx_index; RootDrvBool ready; } State;
static State state;
static RxDesc rx_desc[RX_COUNT] __attribute__((aligned(16)));
static TxDesc tx_desc[TX_COUNT] __attribute__((aligned(16)));
static RootDrvU8 rx_buf[RX_COUNT][BUF_SIZE] __attribute__((aligned(16)));
static RootDrvU8 tx_buf[TX_COUNT][BUF_SIZE] __attribute__((aligned(16)));

static void zero(void* p, RootDrvSize n){ RootDrvU8* x=p; for(RootDrvSize i=0;i<n;i++) x[i]=0; }
static void copy(void* d,const void* s,RootDrvSize n){ RootDrvU8* a=d; const RootDrvU8* b=s; for(RootDrvSize i=0;i<n;i++) a[i]=b[i]; }
static RootDrvU32 r32(RootDrvU32 o){return state.api->mmio_read32(state.mmio,o);} static void w32(RootDrvU32 o,RootDrvU32 v){state.api->mmio_write32(state.mmio,o,v);}
static RootDrvBool mac_ok(void){ RootDrvBool any=0,allff=1; for(int i=0;i<6;i++){if(state.mac[i]) any=1;if(state.mac[i]!=0xff) allff=0;} return any&&!allff; }
static RootDrvBool ready(void* c){(void)c;return state.ready;} static RootDrvBool link(void* c){(void)c;return state.ready && (r32(REG_STATUS)&2u)!=0u;}
static RootDrvBool send(void* c,const void* data,RootDrvSize size){(void)c;if(!state.ready||!data||!size||size>BUF_SIZE)return 0;TxDesc* d=&tx_desc[state.tx_index];if(!(d->status&TX_DD))return 0;copy(tx_buf[state.tx_index],data,size);d->length=(RootDrvU16)size;d->cso=0;d->command=TX_EOP|TX_IFCS|TX_RS;d->status=0;d->css=0;d->special=0;RootDrvU32 cur=state.tx_index;state.tx_index=(state.tx_index+1u)&(TX_COUNT-1u);state.api->memory_barrier();w32(REG_TDT,state.tx_index);for(RootDrvU32 i=0;i<100000u;i++){if(tx_desc[cur].status&TX_DD)return 1;state.api->cpu_pause();}return 0;}
static RootDrvBool receive(void* c,void* out,RootDrvSize cap,RootDrvSize* result){(void)c;if(result)*result=0;if(!state.ready||!out)return 0;RxDesc* d=&rx_desc[state.rx_index];if(!(d->status&RX_DD))return 0;RootDrvSize len=d->length;RootDrvBool ok=(d->status&RX_EOP)&&d->errors==0&&len<=cap; if(ok){copy(out,rx_buf[state.rx_index],len);if(result)*result=len;}d->status=0;d->errors=0;RootDrvU32 done=state.rx_index;state.rx_index=(state.rx_index+1u)&(RX_COUNT-1u);state.api->memory_barrier();w32(REG_RDT,done);return ok;}
static const RootNetDriverOps ops={ready,link,send,receive};

int root_driver_entry(const RootDriverApi* api,const RootDriverDeviceInfo* dev){
 if(!api||api->abi_version!=ROOT_DRIVER_ABI_VERSION||!dev||dev->vendor_id!=0x8086u||dev->device_id!=0x100eu)return ROOT_DRIVER_ERROR;
 zero(&state,sizeof(state));state.api=api; if(!dev->bars[0].present||dev->bars[0].type==ROOT_DRIVER_BAR_IO||dev->bars[0].base>0xffffffffull)return ROOT_DRIVER_ERROR;state.mmio=dev->bars[0].base;
 if(!api->pci_enable_memory(dev->pci_bus,dev->pci_device,dev->pci_function)||!api->pci_enable_bus_master(dev->pci_bus,dev->pci_device,dev->pci_function))return ROOT_DRIVER_ERROR;
 w32(REG_IMC,0xffffffffu);(void)r32(REG_ICR);w32(REG_CTRL,r32(REG_CTRL)|CTRL_SLU);RootDrvU32 lo=r32(REG_RAL0),hi=r32(REG_RAH0);state.mac[0]=lo;state.mac[1]=lo>>8;state.mac[2]=lo>>16;state.mac[3]=lo>>24;state.mac[4]=hi;state.mac[5]=hi>>8;if(!mac_ok())return ROOT_DRIVER_ERROR;
 zero(rx_desc,sizeof(rx_desc));zero(tx_desc,sizeof(tx_desc));for(RootDrvU32 i=0;i<RX_COUNT;i++){rx_desc[i].address=(RootDrvU64)(RootDrvU32)(RootDrvSize)&rx_buf[i][0];}for(RootDrvU32 i=0;i<TX_COUNT;i++){tx_desc[i].address=(RootDrvU64)(RootDrvU32)(RootDrvSize)&tx_buf[i][0];tx_desc[i].status=TX_DD;}
 RootDrvU64 rb=(RootDrvU64)(RootDrvU32)(RootDrvSize)&rx_desc[0],tb=(RootDrvU64)(RootDrvU32)(RootDrvSize)&tx_desc[0];w32(REG_RDBAL,(RootDrvU32)rb);w32(REG_RDBAH,(RootDrvU32)(rb>>32));w32(REG_RDLEN,sizeof(rx_desc));w32(REG_RDH,0);w32(REG_RDT,RX_COUNT-1);w32(REG_TDBAL,(RootDrvU32)tb);w32(REG_TDBAH,(RootDrvU32)(tb>>32));w32(REG_TDLEN,sizeof(tx_desc));w32(REG_TDH,0);w32(REG_TDT,0);w32(REG_RCTL,RCTL_EN|RCTL_BAM|RCTL_SECRC);w32(REG_TCTL,TCTL_EN|TCTL_PSP|(0x10u<<4)|(0x40u<<12));w32(REG_TIPG,10u|(8u<<10)|(6u<<20));state.ready=1;
 return api->net_register("e1000",&state,&ops,state.mac)?ROOT_DRIVER_OK:ROOT_DRIVER_ERROR;
}
