//spike -- this file contains prototypes+etc for voice chat.
//it should be fairly straight forward to integrate this into other engines, however, to implement it properly you'll need to deal with the whole pext2_voicechat handshake thing.
//for quakespasm-spiked this is already handled for entity deltas etc.
//you'll also need to figure out something with the 4 clientcommands that servers might receive.
//to test, cl_voip_test 1;sv_voip_echo 0;+voip should start playing even without any protocol extensions. then move on to cl_voip_test 0;sv_voip_echo 1;+voip once you have protocol stuff working.
//you'll also want to add the various voip settings to the menu, especially cl_voip_play (slider 0-1), cl_voip_send (boolean), +voip binding.

//defined elsewhere
//#define svcfte_voicechat	84
//#define clcfte_voicechat	83
struct client_s;


//client functions
void S_Voip_Transmit(unsigned char clc, sizebuf_t *buf);	//call from CL_SendMove (null buf if not connecting, grabs new data, encodes, and writes into the buffer)
void S_Voip_MapChange(void);								//call from end of CL_ParseServerinfo (tells server to reenable voice chat)
void S_Voip_Parse(void);									//call from CL_ParseServerMessage+svcfte_voicechat. processes voip data from the server
int S_Voip_Loudness(qboolean ignorevad);					//for sbar stuff, if you want to draw some mic-level bar (returns 0-100, or -1 for not transmitting)
qboolean S_Voip_Speaking(unsigned int plno);				//for sbar stuff, if you want to query which other players are speaking (add a scoreboard back-colour or something).
void S_Voip_Init(void);										//call from S_Init, registers client cvars+commands

//server functions
void SV_VoiceInit(void);									//call from SV_Init, registers server cvars+commands
void SV_VoiceInitClient(struct client_s *client);					//call from start of SV_SendServerinfo, disables voice chat until the client is ready to re-enable it
void SV_VoiceSendPacket(struct client_s *client, sizebuf_t *buf);	//call from near end of SV_SendClientDatagram, to forward voice data to other clients
void SV_VoiceReadPacket(struct client_s *client);					//call from SV_ReadClientMessage+clcfte_voicechat. processes voip data from clients and figures out which clients to forward to
typedef struct
{
	unsigned int read;	/*place in ring*/
	unsigned char mute[MAX_SCOREBOARD/8]; /*which other clients should be muted for this player, reducing bandwidth from annoying cunts*/
	qboolean active;		/*client wants to hear other people*/
	enum
	{
		/*should we add one to respond to the last speaker? or should that be an automagic +voip_reply instead?*/
		VT_TEAM,
		VT_ALL,
		VT_NONMUTED,	/*cheap, but allows custom private channels with no external pesters*/
		VT_PLAYERSLOT0
		/*player0+...*/
	} target;
} client_voip_t;	//embedded within struct client_s as a member named voip
