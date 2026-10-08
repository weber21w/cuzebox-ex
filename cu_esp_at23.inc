/* ESP-AT 2.3 compatibility handlers. Included by cu_esp_at.c after the
 * shared parser helpers so legacy NONOS behavior can remain selectable. */

static uint8 cu_esp_at23_enabled(void)
{
	return (esp_state.at_firmware_profile == CU_ESP_AT_FW_ESPAT_230) ? 1u : 0u;
}

static void cu_esp_at23_store_if_enabled(void)
{
	if(esp_state.sysstore_mode){
		esp_state.flash_dirty = 1u;
		cu_esp_save_config();
	}
}

static int cu_esp_parse_hex_u32_local(const sint8 **pp, uint32 *out)
{
	const sint8 *p = cu_esp_skip_ws(*pp);
	char *end = NULL;
	unsigned long v;
	if(!p || !*p) return 0;
	v = strtoul((const char *)p, &end, 0);
	if(end == (char *)p) return 0;
	*out = (uint32)v;
	*pp = cu_esp_skip_ws((const sint8 *)end);
	return 1;
}

static int cu_esp_parse_mac6_at(const char *s, uint8 out[6])
{
	unsigned v[6];
	char tail;
	if(!s || sscanf(s, "%2x:%2x:%2x:%2x:%2x:%2x%c",
		&v[0], &v[1], &v[2], &v[3], &v[4], &v[5], &tail) != 6) return 0;
	for(int i=0;i<6;i++) out[i]=(uint8)v[i];
	return 1;
}

void cu_esp_at_cmd(sint8 *cmd_buf)
{
	static const struct { const char *name; uint8 t,q,s,e; } cmds[] = {
		{"AT",0,0,0,1},{"AT+RST",0,0,0,1},{"AT+GMR",0,0,0,1},{"AT+CMD",0,1,0,0},
		{"AT+GSLP",0,0,1,0},{"ATE",0,0,1,0},{"AT+RESTORE",0,0,0,1},
		{"AT+UART_CUR",0,1,1,0},{"AT+UART_DEF",0,1,1,0},{"AT+SLEEP",0,1,1,0},
		{"AT+SYSRAM",0,1,0,0},{"AT+SYSMSG",0,1,1,0},{"AT+SYSFLASH",0,1,1,0},
		{"AT+RFPOWER",0,1,1,0},{"AT+SYSROLLBACK",0,0,0,1},{"AT+SYSTIMESTAMP",0,1,1,0},
		{"AT+SYSLOG",0,1,1,0},{"AT+SLEEPWKCFG",0,0,1,0},{"AT+SYSSTORE",0,1,1,0},
		{"AT+SYSREG",0,0,1,0},{"AT+CWMODE",1,1,1,0},{"AT+CWSTATE",0,1,0,0},
		{"AT+CWJAP",0,1,1,1},{"AT+CWRECONNCFG",0,1,1,0},{"AT+CWLAPOPT",0,0,1,0},
		{"AT+CWLAP",1,0,1,1},{"AT+CWQAP",0,0,0,1},{"AT+CWSAP",0,1,1,0},
		{"AT+CWLIF",0,0,0,1},{"AT+CWQIF",0,0,1,1},{"AT+CWDHCP",0,1,1,0},{"AT+CWDHCPS",1,1,1,0},
		{"AT+CWAUTOCONN",0,1,1,0},{"AT+CWAPPROTO",0,1,1,0},{"AT+CWSTAPROTO",0,1,1,0},
		{"AT+CIPSTAMAC",0,1,1,0},{"AT+CIPAPMAC",0,1,1,0},{"AT+CIPSTA",0,1,1,0},{"AT+CIPAP",0,1,1,0},
		{"AT+CWSTARTSMART",0,0,1,1},{"AT+CWSTOPSMART",0,0,0,1},{"AT+WPS",0,0,1,0},{"AT+MDNS",0,0,1,0},
		{"AT+CWHOSTNAME",0,1,1,0},{"AT+CWCOUNTRY",0,1,1,0},{"AT+CIPSTATUS",0,0,0,1},
		{"AT+CIPSTATE",0,1,0,0},{"AT+CIPDOMAIN",0,0,1,0},{"AT+CIPSTART",1,0,1,0},
		{"AT+CIPSTARTEX",0,0,1,0},{"AT+CIPSEND",1,0,1,0},{"AT+CIPSENDEX",0,0,1,0},
		{"AT+CIPCLOSE",1,0,1,0},{"AT+CIFSR",0,0,0,1},{"AT+CIPMUX",1,1,1,0},{"AT+CIPMODE",1,1,1,0},
		{"AT+CIPSERVER",0,1,1,0},{"AT+CIPSERVERMAXCONN",0,1,1,0},{"AT+CIPSTO",0,1,1,0},
		{"AT+CIPSNTPCFG",0,1,1,0},{"AT+CIPSNTPTIME",0,1,0,0},{"AT+CIUPDATE",0,0,1,0},{"AT+CIPDINFO",0,1,1,0},
		{"AT+CIPRECVMODE",0,1,1,0},{"AT+CIPRECVLEN",0,1,1,0},{"AT+CIPRECVDATA",0,0,1,0},
		{"AT+CIPV6",0,1,1,0},{"AT+CIPRECONNINTV",0,1,1,0},{"AT+CIPDNS",0,1,1,0},{"AT+CIPTCPOPT",0,1,1,0},
		{"AT+CIPSSLCCONF",0,1,1,0},{"AT+CIPSSLCCN",0,1,1,0},{"AT+CIPSSLCSNI",0,1,1,0},
		{"AT+CIPSSLCALPN",0,1,1,0},{"AT+CIPSSLCPSK",0,1,1,0},{"AT+CIPSSLCCIPHER",0,1,1,0},
		{"AT+SAVETRANSLINK",0,0,1,0},{"AT+PING",0,0,1,0},
		{"AT+MQTTUSERCFG",0,1,1,0},{"AT+MQTTLONGCLIENTID",0,1,1,0},{"AT+MQTTLONGUSERNAME",0,1,1,0},
		{"AT+MQTTLONGPASSWORD",0,1,1,0},{"AT+MQTTCONNCFG",0,1,1,0},{"AT+MQTTCONN",0,1,1,0},
		{"AT+MQTTPUB",0,0,1,0},{"AT+MQTTPUBRAW",0,0,1,0},{"AT+MQTTSUB",0,1,1,0},
		{"AT+MQTTUNSUB",0,0,1,0},{"AT+MQTTCLEAN",0,0,0,1}
	};
	(void)cmd_buf;
	if(!cu_esp_at23_enabled()){ cu_esp_at_bad_command(); return; }
	for(auint i=0;i<CU_ARRLEN(cmds);i++){
		cu_esp_txp("+CMD:"); cu_esp_txi((sint32)i); cu_esp_txp(",\""); cu_esp_txp(cmds[i].name);
		cu_esp_txp("\","); cu_esp_txi(cmds[i].t); cu_esp_txp(","); cu_esp_txi(cmds[i].q);
		cu_esp_txp(","); cu_esp_txi(cmds[i].s); cu_esp_txp(","); cu_esp_txi(cmds[i].e); cu_esp_txp("\r\n");
	}
	cu_esp_txp_ok();
}

void cu_esp_at_sysstore(sint8 *cmd_buf)
{
	const char *s=(const char*)cmd_buf; const char *q=strchr(s,'?'); const char *e=strchr(s,'=');
	if(q && (!e || q<e)){ cu_esp_txp("+SYSSTORE:"); cu_esp_txi(esp_state.sysstore_mode?1:0); cu_esp_txp("\r\n"); cu_esp_txp_ok(); return; }
	if(e){ const sint8 *p=(const sint8*)e+1; int v; if(!cu_esp_parse_bool01(&p,&v)||!cu_esp_expect_crlf(p)){cu_esp_txp_error();return;} esp_state.sysstore_mode=(uint8)v; cu_esp_txp_ok(); return; }
	cu_esp_txp_error();
}

void cu_esp_at_systimestamp(sint8 *cmd_buf)
{
	const char *s=(const char*)cmd_buf; const char *q=strchr(s,'?'); const char *e=strchr(s,'=');
	if(q && (!e||q<e)){
		uint32 now=(uint32)time(NULL); uint32 base=esp_state.systimestamp_value;
		if(esp_state.systimestamp_host && now>=esp_state.systimestamp_host) base += now-esp_state.systimestamp_host;
		cu_esp_txp("+SYSTIMESTAMP:"); cu_esp_txi((sint32)base); cu_esp_txp("\r\n"); cu_esp_txp_ok(); return;
	}
	if(e){ const sint8 *p=(const sint8*)e+1; uint32 v; if(!cu_esp_parse_hex_u32_local(&p,&v)||!cu_esp_expect_crlf(p)){cu_esp_txp_error();return;} esp_state.systimestamp_value=v; esp_state.systimestamp_host=(uint32)time(NULL); cu_esp_txp_ok(); return; }
	cu_esp_txp_error();
}

void cu_esp_at_syslog(sint8 *cmd_buf)
{
	const char *s=(const char*)cmd_buf; const char *q=strchr(s,'?'); const char *e=strchr(s,'=');
	if(q && (!e||q<e)){ cu_esp_txp("+SYSLOG:"); cu_esp_txi(esp_state.syslog_enabled?1:0); cu_esp_txp("\r\n"); cu_esp_txp_ok(); return; }
	if(e){ const sint8 *p=(const sint8*)e+1; int v; if(!cu_esp_parse_bool01(&p,&v)||!cu_esp_expect_crlf(p)){cu_esp_txp_error();return;} esp_state.syslog_enabled=(uint8)v; cu_esp_txp_ok(); return; }
	cu_esp_txp_error();
}

void cu_esp_at_sysflash(sint8 *cmd_buf)
{
	const char *s=(const char*)cmd_buf; const char *q=strchr(s,'?'); const char *e=strchr(s,'=');
	if(q && (!e||q<e)){
		cu_esp_txp("+SYSFLASH:\"" CU_ESP_SYSFLASH_PART_NAME "\",64,0,0,65536\r\n"); cu_esp_txp_ok(); return;
	}
	if(!e){cu_esp_txp_error();return;}
	{
		const sint8 *p=(const sint8*)e+1; int op; sint8 part[32]; auint off=0,len=0;
		if(!cu_esp_parse_u16_dec(&p,&op)||op<0||op>2||!cu_esp_expect_char(&p,',')||!cu_esp_parse_quoted_str(&p,part,sizeof(part),0)){cu_esp_txp_error();return;}
		if(strcmp((char*)part,CU_ESP_SYSFLASH_PART_NAME)){cu_esp_txp_error();return;}
		if(*p==',' && op==0){ /* explicit or whole-part erase */ }
		if(*p==','){
			p++; if(!cu_esp_parse_u32_8(p,&off,&p)||!cu_esp_expect_char(&p,',')||!cu_esp_parse_u32_8(p,&len,&p)){cu_esp_txp_error();return;}
		}else if(op==0){ off=0; len=CU_ESP_SYSFLASH_PART_SIZE; }
		else {cu_esp_txp_error();return;}
		if(!cu_esp_expect_crlf(p)||off>CU_ESP_SYSFLASH_PART_SIZE||len>CU_ESP_SYSFLASH_PART_SIZE-off){cu_esp_txp_error();return;}
		if(op==0){ if((off&4095u)||(len&4095u)){cu_esp_txp_error();return;} memset(&esp_state.sysflash_data[off],0xFF,len); esp_state.sysflash_erased=1u; cu_esp_txp_ok(); return; }
		if(op==2){ cu_esp_txp("+SYSFLASH:"); cu_esp_txi((sint32)len); cu_esp_txp(","); if(len)cu_esp_txl((const char*)&esp_state.sysflash_data[off],len); cu_esp_txp("\r\n"); cu_esp_txp_ok(); return; }
		if(len>sizeof(esp_state.write_buf)){cu_esp_txp_error();return;}
		esp_state.sysflash_write_active=1u; esp_state.sysflash_write_offset=(uint16)off; esp_state.sysflash_write_length=len;
		esp_state.mqtt_input_kind=CU_ESP_MQTT_INPUT_SYSFLASH; esp_state.mqtt_input_expected=(uint16)len;
		esp_state.rx_await_bytes=len; esp_state.write_buf_pos_in=0u; cu_esp_txp(">\r\n"); return;
	}
}

void cu_esp_at_sysrollback(sint8 *cmd_buf){
	if(strstr((char*)cmd_buf,"AT+SYSROLLBACK\r\n")==(char*)cmd_buf) cu_esp_txp_ok(); else cu_esp_txp_error();
}

void cu_esp_at_sleepwkcfg(sint8 *cmd_buf)
{
	const char *e=strchr((char*)cmd_buf,'='); if(!e){cu_esp_txp_error();return;}
	const sint8 *p=(const sint8*)e+1; int src,gpio,level=0;
	if(!cu_esp_parse_u16_dec(&p,&src)||src!=2||!cu_esp_expect_char(&p,',')||!cu_esp_parse_u16_dec(&p,&gpio)||gpio<0||gpio>=ESP_GPIO_PIN_COUNT||gpio==16){cu_esp_txp_error();return;}
	if(*p==','){p++; if(!cu_esp_parse_u16_dec(&p,&level)||level<0||level>1){cu_esp_txp_error();return;}}
	if(!cu_esp_expect_crlf(p)){cu_esp_txp_error();return;}
	esp_state.sleepwk_source=(uint8)src; esp_state.sleepwk_gpio=(uint8)gpio; esp_state.sleepwk_level=(uint8)level; cu_esp_txp_ok();
}

void cu_esp_at_sysreg(sint8 *cmd_buf)
{
	const char *e=strchr((char*)cmd_buf,'='); if(!e){cu_esp_txp_error();return;}
	const sint8 *p=(const sint8*)e+1; int dir; uint32 addr,val=0; int have_val=0;
	if(!cu_esp_parse_u16_dec(&p,&dir)||(dir!=0&&dir!=1)||!cu_esp_expect_char(&p,',')||!cu_esp_parse_hex_u32_local(&p,&addr)){cu_esp_txp_error();return;}
	if(*p==','){p++; if(!cu_esp_parse_hex_u32_local(&p,&val)){cu_esp_txp_error();return;} have_val=1;}
	if(!cu_esp_expect_crlf(p)||((dir==1)!=have_val)){cu_esp_txp_error();return;}
	auint slot=CU_ESP_SYSREG_SLOTS;
	for(auint i=0;i<CU_ESP_SYSREG_SLOTS;i++) if(esp_state.sysreg_valid[i]&&esp_state.sysreg_addr[i]==addr){slot=i;break;}
	if(dir==1){ if(slot==CU_ESP_SYSREG_SLOTS){for(auint i=0;i<CU_ESP_SYSREG_SLOTS;i++)if(!esp_state.sysreg_valid[i]){slot=i;break;}} if(slot==CU_ESP_SYSREG_SLOTS){cu_esp_txp_error();return;} esp_state.sysreg_valid[slot]=1;esp_state.sysreg_addr[slot]=addr;esp_state.sysreg_value[slot]=val;cu_esp_txp_ok();return; }
	if(slot!=CU_ESP_SYSREG_SLOTS) val=esp_state.sysreg_value[slot]; else val=0;
	{char b[32];snprintf(b,sizeof(b),"+SYSREG:0x%08X\r\n",(unsigned)val);cu_esp_txp(b);} cu_esp_txp_ok();
}

void cu_esp_at_cwstate(sint8 *cmd_buf)
{
	if(!strchr((char*)cmd_buf,'?')){cu_esp_txp_error();return;}
	int st=(esp_state.state&ESP_AP_CONNECTED)?2:(esp_state.wifi_join_pending?3:4);
	cu_esp_txp("+CWSTATE:");cu_esp_txi(st);cu_esp_txp(",\""); if(st==2||st==3)cu_esp_txp((char*)esp_state.wifi_name);cu_esp_txp("\"\r\n");cu_esp_txp_ok();
}

void cu_esp_at_cwreconncfg(sint8 *cmd_buf)
{
	const char *q=strchr((char*)cmd_buf,'?'),*e=strchr((char*)cmd_buf,'=');
	if(q&&(!e||q<e)){cu_esp_txp("+CWRECONNCFG:");cu_esp_txi(esp_state.wifi_reconn_interval);cu_esp_txp(",");cu_esp_txi(esp_state.wifi_reconn_repeat);cu_esp_txp("\r\n");cu_esp_txp_ok();return;}
	if(e){const sint8*p=(const sint8*)e+1;int a,b;if(!cu_esp_parse_u16_dec(&p,&a)||a<0||a>7200||!cu_esp_expect_char(&p,',')||!cu_esp_parse_u16_dec(&p,&b)||b<0||b>1000||!cu_esp_expect_crlf(p)){cu_esp_txp_error();return;}esp_state.wifi_reconn_interval=(uint16)a;esp_state.wifi_reconn_repeat=(uint16)b;cu_esp_at23_store_if_enabled();cu_esp_txp_ok();return;}cu_esp_txp_error();
}

static void cu_esp_at_wifi_proto_common(sint8 *cmd_buf,uint8 *field,const char *tag)
{
	const char*q=strchr((char*)cmd_buf,'?'),*e=strchr((char*)cmd_buf,'=');
	if(q&&(!e||q<e)){cu_esp_txp(tag);cu_esp_txi(*field);cu_esp_txp("\r\n");cu_esp_txp_ok();return;}
	if(e){const sint8*p=(const sint8*)e+1;int v;if(!cu_esp_parse_u16_dec(&p,&v)||!(v==1||v==3||v==7)||!cu_esp_expect_crlf(p)){cu_esp_txp_error();return;}*field=(uint8)v;cu_esp_at23_store_if_enabled();cu_esp_txp_ok();return;}cu_esp_txp_error();
}
void cu_esp_at_cwapproto(sint8 *c){cu_esp_at_wifi_proto_common(c,&esp_state.wifi_ap_proto,"+CWAPPROTO:");}
void cu_esp_at_cwstaproto(sint8 *c){cu_esp_at_wifi_proto_common(c,&esp_state.wifi_sta_proto,"+CWSTAPROTO:");}

void cu_esp_at_cwqif(sint8 *cmd_buf)
{
	const char*e=strchr((char*)cmd_buf,'=');
	if(!e){ if(!strncmp((char*)cmd_buf,"AT+CWQIF\r\n",10)){cu_esp_ap_reset_leases(&esp_state);cu_esp_txp_ok();return;} cu_esp_txp_error();return; }
	const sint8*p=(const sint8*)e+1; sint8 macs[24]; uint8 mac[6];
	if(!cu_esp_parse_quoted_str(&p,macs,sizeof(macs),0)||!cu_esp_expect_crlf(p)||!cu_esp_parse_mac6_at((char*)macs,mac)){cu_esp_txp_error();return;}
	cu_esp_ap_release_lease(&esp_state,mac);cu_esp_txp_ok();
}

void cu_esp_at_cipv6(sint8 *cmd_buf)
{
	const char*q=strchr((char*)cmd_buf,'?'),*e=strchr((char*)cmd_buf,'=');
	if(q&&(!e||q<e)){cu_esp_txp("+CIPV6:");cu_esp_txi(esp_state.ipv6_enabled?1:0);cu_esp_txp("\r\n");cu_esp_txp_ok();return;}
	if(e){const sint8*p=(const sint8*)e+1;int v;if(!cu_esp_parse_bool01(&p,&v)||!cu_esp_expect_crlf(p)){cu_esp_txp_error();return;}esp_state.ipv6_enabled=(uint8)v;cu_esp_at23_store_if_enabled();cu_esp_txp_ok();return;}cu_esp_txp_error();
}

void cu_esp_at_cipreconnintv(sint8 *cmd_buf)
{
	const char*q=strchr((char*)cmd_buf,'?'),*e=strchr((char*)cmd_buf,'=');
	if(q&&(!e||q<e)){cu_esp_txp("+CIPRECONNINTV:");cu_esp_txi(esp_state.cip_reconn_interval);cu_esp_txp("\r\n");cu_esp_txp_ok();return;}
	if(e){const sint8*p=(const sint8*)e+1;int v;if(!cu_esp_parse_u16_dec(&p,&v)||v<1||v>36000||!cu_esp_expect_crlf(p)){cu_esp_txp_error();return;}esp_state.cip_reconn_interval=(uint16)v;cu_esp_at23_store_if_enabled();cu_esp_txp_ok();return;}cu_esp_txp_error();
}

void cu_esp_at_ciptcpopt(sint8 *cmd_buf)
{
	const char*q=strchr((char*)cmd_buf,'?'),*e=strchr((char*)cmd_buf,'=');
	if(q&&(!e||q<e)){
		for(uint8 i=0;i<ESP_MAX_LINKS;i++){cu_esp_txp("+CIPTCPOPT:");cu_esp_txi(i);cu_esp_txp(",");cu_esp_txi(esp_state.tcp_linger[i]);cu_esp_txp(",");cu_esp_txi(esp_state.tcp_nodelay[i]);cu_esp_txp(",");cu_esp_txi((sint32)esp_state.tcp_sndtimeo[i]);cu_esp_txp("\r\n");}cu_esp_txp_ok();return;
	}
	if(!e){cu_esp_txp_error();return;}
	const sint8*p=(const sint8*)e+1; int link=0,linger=-32768,nd=-1; uint32 to=0; int have_to=0;
	if(esp_state.state&ESP_MUX){if(!cu_esp_parse_u16_dec(&p,&link)||link<0||link>5||!cu_esp_expect_char(&p,',')){cu_esp_txp_error();return;}}
	if(*p!=','){sint32 x;if(!cu_esp_parse_s32_dec(&p,&x)||x<-1||x>7200){cu_esp_txp_error();return;}linger=(int)x;} if(!cu_esp_expect_char(&p,',')){cu_esp_txp_error();return;}
	if(*p!=','){int x;if(!cu_esp_parse_u16_dec(&p,&x)||x<0||x>1){cu_esp_txp_error();return;}nd=x;} if(!cu_esp_expect_char(&p,',')){cu_esp_txp_error();return;}
	if(*p!='\r'){auint x;if(!cu_esp_parse_u32_8(p,&x,&p)){cu_esp_txp_error();return;}to=x;have_to=1;} if(!cu_esp_expect_crlf(p)){cu_esp_txp_error();return;}
	for(uint8 i=0;i<ESP_MAX_LINKS;i++){if(link!=5&&i!=(uint8)link)continue;if(linger!=-32768)esp_state.tcp_linger[i]=(sint16)linger;if(nd>=0)esp_state.tcp_nodelay[i]=(uint8)nd;if(have_to)esp_state.tcp_sndtimeo[i]=to;cu_esp_net_reapply_tcp_options(i);}cu_esp_at23_store_if_enabled();cu_esp_txp_ok();
}

void cu_esp_at_cipstate(sint8 *cmd_buf)
{
	if(!strchr((char*)cmd_buf,'?')){cu_esp_txp_error();return;}
	for(uint8 i=0;i<ESP_MAX_LINKS;i++){
		if(esp_state.socks[i]==ESP_INVALID_SOCKET && !cu_esp_lan_virtual_link_present(&esp_state,i)) continue;
		char ip[80]="0.0.0.0";uint16 rp=0;cu_esp_net_get_peer_text(i,ip,sizeof(ip),&rp);
		const char*pt=(esp_state.protocol[i]&ESP_PROTO_SSL)?((esp_state.protocol[i]&ESP_PROTO_IPV6)?"SSLv6":"SSL"):(esp_state.protocol[i]&ESP_PROTO_UDP)?((esp_state.protocol[i]&ESP_PROTO_IPV6)?"UDPv6":"UDP"):((esp_state.protocol[i]&ESP_PROTO_IPV6)?"TCPv6":"TCP");
		cu_esp_txp("+CIPSTATE:");cu_esp_txi(i);cu_esp_txp(",\"");cu_esp_txp(pt);cu_esp_txp("\",\"");cu_esp_txp(ip);cu_esp_txp("\",");cu_esp_txi(rp);cu_esp_txp(",");cu_esp_txi(cu_esp_net_get_local_port(i));cu_esp_txp(",");cu_esp_txi(esp_state.link_is_server[i]?1:0);cu_esp_txp("\r\n");
	}cu_esp_txp_ok();
}

void cu_esp_at_cipstartex(sint8 *cmd_buf)
{
	if(!cu_esp_at23_enabled()){cu_esp_at_bad_command();return;}
	/* Let the established CIPSTART parser do the hard work after selecting
	 * the first free link, preserving exact connection semantics. */
	if(!(esp_state.state&ESP_MUX)){cu_esp_txp_error();return;}
	uint8 id=ESP_MAX_LINKS; for(uint8 i=0;i<ESP_MAX_LINKS;i++) if(esp_state.socks[i]==ESP_INVALID_SOCKET&&!cu_esp_lan_virtual_link_present(&esp_state,i)){id=i;break;}
	if(id>=ESP_MAX_LINKS){cu_esp_txp_error();return;}
	const char*e=strchr((char*)cmd_buf,'=');if(!e){cu_esp_txp_error();return;}
	char tmp[512];snprintf(tmp,sizeof(tmp),"AT+CIPSTART=%u,%s",(unsigned)id,e+1);cu_esp_at_cipstart((sint8*)tmp);
}

static const char *cu_esp_cipher_name(uint16 id)
{
	switch(id){case 0x002F:return "AES128-SHA";case 0x0035:return "AES256-SHA";case 0x003C:return "AES128-SHA256";case 0x003D:return "AES256-SHA256";case 0x009C:return "AES128-GCM-SHA256";case 0x009D:return "AES256-GCM-SHA384";case 0xC013:return "ECDHE-RSA-AES128-SHA";case 0xC014:return "ECDHE-RSA-AES256-SHA";case 0xC027:return "ECDHE-RSA-AES128-SHA256";case 0xC028:return "ECDHE-RSA-AES256-SHA384";case 0xC02F:return "ECDHE-RSA-AES128-GCM-SHA256";case 0xC030:return "ECDHE-RSA-AES256-GCM-SHA384";default:return NULL;}
}

void cu_esp_at_cipsslccipher(sint8 *cmd_buf)
{
	static const uint16 supported[]={0x002F,0x0035,0x003C,0x003D,0x009C,0x009D,0xC013,0xC014,0xC027,0xC028,0xC02F,0xC030};
	const char*q=strchr((char*)cmd_buf,'?'),*e=strchr((char*)cmd_buf,'=');
	if(q&&(!e||q<e)){
		/* ESP-AT query reports the cipher suites supported by the client, not
		 * the per-link configured filter. */
		for(uint8 i=0;i<(uint8)(sizeof(supported)/sizeof(supported[0]));i++){char bfr[32];snprintf(bfr,sizeof(bfr),"+CIPSSLCCIPHER:%u,0x%04X\r\n",(unsigned)i,(unsigned)supported[i]);cu_esp_txp(bfr);}cu_esp_txp_ok();return;
	}
	if(!e){cu_esp_txp_error();return;}
	const sint8*p=(const sint8*)e+1;uint8 target=0;int cnt=0;
	if(!cu_esp_ssl_parse_target(&p,&target)||!cu_esp_parse_u16_dec(&p,&cnt)||cnt<0||cnt>12){cu_esp_txp_error();return;}
	uint16 vals[12]={0};
	for(int j=0;j<cnt;j++){uint32 x;if(!cu_esp_expect_char(&p,',')||!cu_esp_parse_hex_u32_local(&p,&x)||x>0xFFFFu||!cu_esp_cipher_name((uint16)x)){cu_esp_txp_error();return;}vals[j]=(uint16)x;}
	if(!cu_esp_expect_crlf(p)){cu_esp_txp_error();return;}
	for(uint8 i=cu_esp_ssl_target_first(target),last=cu_esp_ssl_target_last(target);i<=last;i++){esp_state.ssl_cipher_count[i]=(uint8)cnt;memcpy(esp_state.ssl_cipher[i],vals,sizeof(vals));}
	cu_esp_txp_ok();
}

void cu_esp_save_translink(void){ cu_esp_save_config(); }

void cu_esp_at_savetranslink(sint8 *cmd_buf)
{
	const char*e=strchr((char*)cmd_buf,'=');if(!e){cu_esp_txp_error();return;}const sint8*p=(const sint8*)e+1;int mode;
	if(!cu_esp_parse_u16_dec(&p,&mode)||(mode!=0&&mode!=1)){cu_esp_txp_error();return;}
	if(mode==0){if(!cu_esp_expect_crlf(p)){cu_esp_txp_error();return;}esp_state.translink_enabled=0;esp_state.translink_boot_pending=0;cu_esp_save_translink();cu_esp_txp_ok();return;}
	sint8 host[128],proto[16];int port,keep=0,local=0,umode=0;if(!cu_esp_expect_char(&p,',')||!cu_esp_parse_quoted_str(&p,host,sizeof(host),0)||!cu_esp_expect_char(&p,',')||!cu_esp_parse_u16_dec(&p,&port)||port<1||port>65535||!cu_esp_expect_char(&p,',')||!cu_esp_parse_quoted_str(&p,proto,sizeof(proto),0)){cu_esp_txp_error();return;}
	uint8 kind=0;if(!strcmp((char*)proto,"TCP"))kind=CU_ESP_TRANSLINK_TCP;else if(!strcmp((char*)proto,"UDP"))kind=CU_ESP_TRANSLINK_UDP;else if(!strcmp((char*)proto,"SSL"))kind=CU_ESP_TRANSLINK_SSL;else if(!strcmp((char*)proto,"TCPv6"))kind=CU_ESP_TRANSLINK_TCP6;else if(!strcmp((char*)proto,"UDPv6"))kind=CU_ESP_TRANSLINK_UDP6;else if(!strcmp((char*)proto,"SSLv6"))kind=CU_ESP_TRANSLINK_SSL6;else {cu_esp_txp_error();return;}
	if((kind>=CU_ESP_TRANSLINK_TCP6)&&!esp_state.ipv6_enabled){cu_esp_txp_error();return;}
	if(*p==','){p++;if(*p!=','&&*p!='\r'){if(!cu_esp_parse_u16_dec(&p,&keep)){cu_esp_txp_error();return;}}if(*p==','){p++;if(*p!=','&&*p!='\r'){if(!cu_esp_parse_u16_dec(&p,&local)){cu_esp_txp_error();return;}}if(*p==','){p++;if(!cu_esp_parse_u16_dec(&p,&umode)){cu_esp_txp_error();return;}}}}
	if(!cu_esp_expect_crlf(p)){cu_esp_txp_error();return;}esp_state.translink_enabled=1;esp_state.translink_kind=kind;esp_state.translink_port=(uint16)port;esp_state.translink_keepalive=(uint16)keep;esp_state.translink_local_port=(uint16)local;esp_state.translink_udp_mode=(uint8)umode;snprintf((char*)esp_state.translink_host,sizeof(esp_state.translink_host),"%s",(char*)host);cu_esp_save_translink();cu_esp_txp_ok();
}

/* MQTT AT front-end. Stock ESP8266 v2.3 firmware supports scheme 1
 * (TCP) and scheme 6 (WebSocket); MQTT-over-TLS schemes are excluded by the
 * ESP8266 build because of its memory limit. */
static int cu_esp_mqtt_link0(const sint8 **p)
{
	int link;
	return cu_esp_parse_u16_dec(p,&link) && link==0;
}

static uint8 cu_esp_mqtt_state_code(void)
{
	if(cu_esp_mqtt_state())
		return cu_esp_mqtt_subscription_count() ? 6u : 5u;
	if(esp_state.mqtt_host[0])
		return 3u;
	if(esp_state.mqtt_conn_configured)
		return 2u;
	if(esp_state.mqtt_user_configured)
		return 1u;
	return 0u;
}

static uint8 cu_esp_mqtt_topic_is_subscribed(const char *topic)
{
	for(uint8 i=0;i<cu_esp_mqtt_subscription_count();i++){
		char t[CU_ESP_MQTT_TOPIC_MAX+1u];
		if(cu_esp_mqtt_subscription_get(i,t,sizeof(t),NULL)==0 && !strcmp(t,topic))
			return 1u;
	}
	return 0u;
}

static uint8 cu_esp_mqtt_pubraw_fits(const char *topic,uint32 payload_len,uint8 qos)
{
	uint32 rem=2u+(uint32)strlen(topic)+(qos?2u:0u)+payload_len;
	uint32 rl=1u,v=rem;
	while(v>=128u){rl++;v>>=7;}
	return (uint8)((1u+rl+rem)<=512u); /* stock ESP8266 MQTT_BUFFER_SIZE_BYTE */
}

void cu_esp_at_mqttusercfg(sint8 *cmd_buf)
{
	const char*q=strchr((char*)cmd_buf,'?'),*e=strchr((char*)cmd_buf,'=');
	if(q&&(!e||q<e)){
		cu_esp_txp("+MQTTUSERCFG:0,");cu_esp_txi(esp_state.mqtt_scheme);cu_esp_txp(",\"");
		cu_esp_txp((char*)esp_state.mqtt_client_id);cu_esp_txp("\",\"");cu_esp_txp((char*)esp_state.mqtt_username);
		cu_esp_txp("\",\"");cu_esp_txp((char*)esp_state.mqtt_password);cu_esp_txp("\",");cu_esp_txi(esp_state.mqtt_cert_key_id);
		cu_esp_txp(",");cu_esp_txi(esp_state.mqtt_ca_id);cu_esp_txp(",\"");cu_esp_txp((char*)esp_state.mqtt_path);cu_esp_txp("\"\r\n");cu_esp_txp_ok();return;
	}
	if(!e){cu_esp_txp_error();return;}
	const sint8*p=(const sint8*)e+1;int scheme,cid,ca;
	sint8 cl[257],us[65],pw[65],path[33];
	if(!cu_esp_mqtt_link0(&p)||!cu_esp_expect_char(&p,',')||!cu_esp_parse_u16_dec(&p,&scheme)||(scheme!=1&&scheme!=6)||
	   !cu_esp_expect_char(&p,',')||!cu_esp_parse_quoted_str0(&p,cl,sizeof(cl))||
	   !cu_esp_expect_char(&p,',')||!cu_esp_parse_quoted_str0(&p,us,sizeof(us))||
	   !cu_esp_expect_char(&p,',')||!cu_esp_parse_quoted_str0(&p,pw,sizeof(pw))||
	   !cu_esp_expect_char(&p,',')||!cu_esp_parse_u16_dec(&p,&cid)||cid!=0||
	   !cu_esp_expect_char(&p,',')||!cu_esp_parse_u16_dec(&p,&ca)||ca!=0||
	   !cu_esp_expect_char(&p,',')||!cu_esp_parse_quoted_str0(&p,path,sizeof(path))||!cu_esp_expect_crlf(p)){
		cu_esp_txp_error();return;
	}
	esp_state.mqtt_scheme=(uint8)scheme;esp_state.mqtt_cert_key_id=0u;esp_state.mqtt_ca_id=0u;
	snprintf((char*)esp_state.mqtt_client_id,sizeof(esp_state.mqtt_client_id),"%s",(char*)cl);
	snprintf((char*)esp_state.mqtt_username,sizeof(esp_state.mqtt_username),"%s",(char*)us);
	snprintf((char*)esp_state.mqtt_password,sizeof(esp_state.mqtt_password),"%s",(char*)pw);
	snprintf((char*)esp_state.mqtt_path,sizeof(esp_state.mqtt_path),"%s",(char*)path);
	esp_state.mqtt_user_configured=1u;cu_esp_txp_ok();
}

static void cu_esp_mqtt_long(sint8 *cmd_buf,uint8 kind,sint8 *dst,auint cap,const char*tag)
{
	const char*q=strchr((char*)cmd_buf,'?'),*e=strchr((char*)cmd_buf,'=');
	if(q&&(!e||q<e)){cu_esp_txp(tag);cu_esp_txp((char*)dst);cu_esp_txp("\r\n");cu_esp_txp_ok();return;}
	if(!e||!esp_state.mqtt_user_configured){cu_esp_txp_error();return;}
	const sint8*p=(const sint8*)e+1;int link,len;
	if(!cu_esp_parse_u16_dec(&p,&link)||link!=0||!cu_esp_expect_char(&p,',')||!cu_esp_parse_u16_dec(&p,&len)||
	   len<1||len>1024||(auint)len>=cap||!cu_esp_expect_crlf(p)){cu_esp_txp_error();return;}
	esp_state.mqtt_input_kind=kind;esp_state.mqtt_input_expected=(uint16)len;esp_state.rx_await_bytes=(auint)len;
	esp_state.write_buf_pos_in=0;cu_esp_txp("OK\r\n>");
}
void cu_esp_at_mqttlongclientid(sint8*c){cu_esp_mqtt_long(c,CU_ESP_MQTT_INPUT_CLIENTID,esp_state.mqtt_client_id,sizeof(esp_state.mqtt_client_id),"+MQTTLONGCLIENTID:0,");}
void cu_esp_at_mqttlongusername(sint8*c){cu_esp_mqtt_long(c,CU_ESP_MQTT_INPUT_USERNAME,esp_state.mqtt_username,sizeof(esp_state.mqtt_username),"+MQTTLONGUSERNAME:0,");}
void cu_esp_at_mqttlongpassword(sint8*c){cu_esp_mqtt_long(c,CU_ESP_MQTT_INPUT_PASSWORD,esp_state.mqtt_password,sizeof(esp_state.mqtt_password),"+MQTTLONGPASSWORD:0,");}

void cu_esp_at_mqttconncfg(sint8 *cmd_buf)
{
	const char*q=strchr((char*)cmd_buf,'?'),*e=strchr((char*)cmd_buf,'=');
	if(q&&(!e||q<e)){cu_esp_txp("+MQTTCONNCFG:0,");cu_esp_txi(esp_state.mqtt_keepalive);cu_esp_txp(",");cu_esp_txi(esp_state.mqtt_disable_clean_session);cu_esp_txp(",\"");cu_esp_txp((char*)esp_state.mqtt_lwt_topic);cu_esp_txp("\",\"");cu_esp_txp((char*)esp_state.mqtt_lwt_msg);cu_esp_txp("\",");cu_esp_txi(esp_state.mqtt_lwt_qos);cu_esp_txp(",");cu_esp_txi(esp_state.mqtt_lwt_retain);cu_esp_txp("\r\n");cu_esp_txp_ok();return;}
	if(!e||!esp_state.mqtt_user_configured){cu_esp_txp_error();return;}
	const sint8*p=(const sint8*)e+1;int keep,clean,qos,ret;sint8 topic[129],msg[65];
	if(!cu_esp_mqtt_link0(&p)||!cu_esp_expect_char(&p,',')||!cu_esp_parse_u16_dec(&p,&keep)||keep<0||keep>7200||
	   !cu_esp_expect_char(&p,',')||!cu_esp_parse_u16_dec(&p,&clean)||clean<0||clean>1||
	   !cu_esp_expect_char(&p,',')||!cu_esp_parse_quoted_str0(&p,topic,sizeof(topic))||
	   !cu_esp_expect_char(&p,',')||!cu_esp_parse_quoted_str0(&p,msg,sizeof(msg))||
	   !cu_esp_expect_char(&p,',')||!cu_esp_parse_u16_dec(&p,&qos)||qos<0||qos>2||
	   !cu_esp_expect_char(&p,',')||!cu_esp_parse_u16_dec(&p,&ret)||ret<0||ret>1||!cu_esp_expect_crlf(p)){cu_esp_txp_error();return;}
	esp_state.mqtt_keepalive=(uint16)keep;esp_state.mqtt_disable_clean_session=(uint8)clean;esp_state.mqtt_lwt_qos=(uint8)qos;esp_state.mqtt_lwt_retain=(uint8)ret;
	snprintf((char*)esp_state.mqtt_lwt_topic,sizeof(esp_state.mqtt_lwt_topic),"%s",(char*)topic);snprintf((char*)esp_state.mqtt_lwt_msg,sizeof(esp_state.mqtt_lwt_msg),"%s",(char*)msg);
	esp_state.mqtt_conn_configured=1u;cu_esp_txp_ok();
}

void cu_esp_at_mqttconn(sint8 *cmd_buf)
{
	const char*q=strchr((char*)cmd_buf,'?'),*e=strchr((char*)cmd_buf,'=');
	if(q&&(!e||q<e)){
		uint8 st=cu_esp_mqtt_state_code();
		cu_esp_txp("+MQTTCONN:0,");cu_esp_txi(st);cu_esp_txp(",");cu_esp_txi(esp_state.mqtt_scheme);cu_esp_txp(",\"");
		cu_esp_txp((char*)esp_state.mqtt_host);cu_esp_txp("\",");cu_esp_txi(esp_state.mqtt_port);cu_esp_txp(",\"");cu_esp_txp((char*)esp_state.mqtt_path);cu_esp_txp("\",");cu_esp_txi(esp_state.mqtt_reconnect);cu_esp_txp("\r\n");cu_esp_txp_ok();return;
	}
	if(!e||!esp_state.mqtt_user_configured){cu_esp_txp_error();return;}
	const sint8*p=(const sint8*)e+1;int port,reconn;sint8 host[129];
	if(!cu_esp_mqtt_link0(&p)||!cu_esp_expect_char(&p,',')||!cu_esp_parse_quoted_str(&p,host,sizeof(host),0)||
	   !cu_esp_expect_char(&p,',')||!cu_esp_parse_u16_dec(&p,&port)||port<1||port>65535||
	   !cu_esp_expect_char(&p,',')||!cu_esp_parse_u16_dec(&p,&reconn)||reconn<0||reconn>1||!cu_esp_expect_crlf(p)){cu_esp_txp_error();return;}
	snprintf((char*)esp_state.mqtt_host,sizeof(esp_state.mqtt_host),"%s",(char*)host);esp_state.mqtt_port=(uint16)port;esp_state.mqtt_reconnect=(uint8)reconn;
	if(cu_esp_mqtt_connect((char*)host,(uint16)port,esp_state.mqtt_scheme?esp_state.mqtt_scheme:1,(char*)esp_state.mqtt_path,(uint8)reconn)!=0){cu_esp_txp_error();return;}
	cu_esp_txp_ok();
}

void cu_esp_at_mqttpub(sint8 *cmd_buf)
{
	const char*e=strchr((char*)cmd_buf,'=');if(!e){cu_esp_txp_error();return;}const sint8*p=(const sint8*)e+1;int qos,ret;sint8 topic[129],data[2049];
	if(!cu_esp_mqtt_link0(&p)||!cu_esp_expect_char(&p,',')||!cu_esp_parse_quoted_str(&p,topic,sizeof(topic),0)||
	   !cu_esp_expect_char(&p,',')||!cu_esp_parse_quoted_str0(&p,data,sizeof(data))||
	   !cu_esp_expect_char(&p,',')||!cu_esp_parse_u16_dec(&p,&qos)||qos<0||qos>2||
	   !cu_esp_expect_char(&p,',')||!cu_esp_parse_u16_dec(&p,&ret)||ret<0||ret>1||!cu_esp_expect_crlf(p)||
	   cu_esp_mqtt_publish((char*)topic,(uint8*)data,(uint32)strlen((char*)data),(uint8)qos,(uint8)ret)!=0){cu_esp_txp_error();return;}
	cu_esp_txp_ok();
}

void cu_esp_at_mqttpubraw(sint8 *cmd_buf)
{
	const char*e=strchr((char*)cmd_buf,'=');if(!e){cu_esp_txp_error();return;}const sint8*p=(const sint8*)e+1;int len,qos,ret;sint8 topic[129];
	if(!cu_esp_mqtt_link0(&p)||!cu_esp_expect_char(&p,',')||!cu_esp_parse_quoted_str(&p,topic,sizeof(topic),0)||
	   !cu_esp_expect_char(&p,',')||!cu_esp_parse_u16_dec(&p,&len)||len<1||len>(int)sizeof(esp_state.write_buf)||
	   !cu_esp_expect_char(&p,',')||!cu_esp_parse_u16_dec(&p,&qos)||qos<0||qos>2||
	   !cu_esp_expect_char(&p,',')||!cu_esp_parse_u16_dec(&p,&ret)||ret<0||ret>1||!cu_esp_expect_crlf(p)||
	   !cu_esp_mqtt_pubraw_fits((char*)topic,(uint32)len,(uint8)qos)){cu_esp_txp_error();return;}
	snprintf((char*)esp_state.mqtt_raw_topic,sizeof(esp_state.mqtt_raw_topic),"%s",(char*)topic);esp_state.mqtt_raw_qos=(uint8)qos;esp_state.mqtt_raw_retain=(uint8)ret;
	esp_state.mqtt_input_kind=CU_ESP_MQTT_INPUT_PUBRAW;esp_state.mqtt_input_expected=(uint16)len;esp_state.rx_await_bytes=(auint)len;esp_state.write_buf_pos_in=0;cu_esp_txp("OK\r\n>");
}

void cu_esp_at_mqttsub(sint8 *cmd_buf)
{
	const char*q=strchr((char*)cmd_buf,'?'),*e=strchr((char*)cmd_buf,'=');
	if(q&&(!e||q<e)){
		for(uint8 i=0;i<cu_esp_mqtt_subscription_count();i++){
			char t[CU_ESP_MQTT_TOPIC_MAX+1u];uint8 qos;
			if(cu_esp_mqtt_subscription_get(i,t,sizeof(t),&qos)==0){cu_esp_txp("+MQTTSUB:0,");cu_esp_txi(cu_esp_mqtt_state_code());cu_esp_txp(",\"");cu_esp_txp(t);cu_esp_txp("\",");cu_esp_txi(qos);cu_esp_txp("\r\n");}
		}
		cu_esp_txp_ok();return;
	}
	if(!e){cu_esp_txp_error();return;}const sint8*p=(const sint8*)e+1;int qos;sint8 topic[129];
	if(!cu_esp_mqtt_link0(&p)||!cu_esp_expect_char(&p,',')||!cu_esp_parse_quoted_str(&p,topic,sizeof(topic),0)||
	   !cu_esp_expect_char(&p,',')||!cu_esp_parse_u16_dec(&p,&qos)||qos<0||qos>2||!cu_esp_expect_crlf(p)){cu_esp_txp_error();return;}
	if(cu_esp_mqtt_topic_is_subscribed((char*)topic)){cu_esp_txp("ALREADY SUBSCRIBE\r\n");cu_esp_txp_ok();return;}
	if(cu_esp_mqtt_subscribe((char*)topic,(uint8)qos)!=0){cu_esp_txp_error();return;}cu_esp_txp_ok();
}

void cu_esp_at_mqttunsub(sint8 *cmd_buf)
{
	const char*e=strchr((char*)cmd_buf,'=');if(!e){cu_esp_txp_error();return;}const sint8*p=(const sint8*)e+1;sint8 topic[129];
	if(!cu_esp_mqtt_link0(&p)||!cu_esp_expect_char(&p,',')||!cu_esp_parse_quoted_str(&p,topic,sizeof(topic),0)||!cu_esp_expect_crlf(p)){cu_esp_txp_error();return;}
	if(!cu_esp_mqtt_topic_is_subscribed((char*)topic)){cu_esp_txp("NO UNSUBSCRIBE\r\n");cu_esp_txp_ok();return;}
	if(cu_esp_mqtt_unsubscribe((char*)topic)!=0){cu_esp_txp_error();return;}cu_esp_txp_ok();
}

void cu_esp_at_mqttclean(sint8 *cmd_buf)
{
	if(strncmp((char*)cmd_buf,"AT+MQTTCLEAN=0\r\n",16)&&strncmp((char*)cmd_buf,"AT+MQTTCLEAN\r\n",14)){cu_esp_txp_error();return;}
	cu_esp_mqtt_disconnect();cu_esp_txp_ok();
}
