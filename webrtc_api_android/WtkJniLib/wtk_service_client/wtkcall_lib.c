#include "iax2-client/iaxclient_lib.h"
#include "iax2-client/iaxclient.h"
#include "../wtk_rtc_api/wtk_rtc_api.h"
#include "wtkcall_lib.h"

#define MAX_CALLS	4
#define USE_PTT 	0

static char* map_state(int state)
{
	static char *map[] = { "unknown", "active", "outgoing", "ringing",
		"complete", "selected", "busy", "transfered", NULL };
	static char states[256]; 	/* buffer to hold ascii states */
	int i, j;
	int next=0;

	states[0] = '\0';
	if( state == 0 )
		return "free";
	for( i=0, j=1; map[i] != NULL; i++, j<<=1 ) {
		if( state & j ) {
			if (next)
				strcat(states, ",");
			strcat(states, map[i]);
			next = 1;
		}
	}
	return states;
}

//Event for iax lib, will pass it to jni by wtkcall_send_jni_event
static int wtkcall_iax_event_callback( iaxc_event e )
{
	switch(e.type) {
		// R - Registration Event
		case IAXC_EVENT_REGISTRATION:
			iaxci_usermsg(IAXC_NOTICE, 
								"wtkcall_iax_event_callback==>R\treg_ID=%d\treply=%d",
								e.ev.reg.id, e.ev.reg.reply);
			wtkcall_perform_registration_callback(e.ev.reg.id, e.ev.reg.reply);
		break;
		
		// S - State Machine Event
		case IAXC_EVENT_STATE: 
			iaxci_usermsg(IAXC_NOTICE, 
							"wtkcall_iax_event_callback==>S\tcallNo=%d\tstate=[0x%x\t%s]\tremote=%.255s\tremote_name=%.255s",
							e.ev.call.callNo,e.ev.call.state,map_state(e.ev.call.state),e.ev.call.remote,e.ev.call.remote_name);

			wtkcall_perform_state_callback(&calls[e.ev.call.callNo].sm,e.ev.call.callNo, e.ev.call.state, e.ev.call.remote_name, e.ev.call.remote);
			if(e.ev.call.state & IAXC_CALL_STATE_COMPLETE)
			{
				//libwtk_start_audio_stream();
				wtkcall_initialize_media();
			}
			if(e.ev.call.state == IAXC_CALL_STATE_FREE)
			{
				libwtk_stop_audio_stream();
			}
		break;

		// T - Text Event
		case IAXC_EVENT_TEXT:
			if(e.ev.text.type==IAXC_TEXT_TYPE_IAX) 
			{
				iaxci_usermsg(IAXC_NOTICE, 
								"wtkcall_iax_event_callback==>T\ttext_type=%d\tcallNo=%d\tmessage=%.255s",
								e.ev.text.type,e.ev.text.callNo,e.ev.text.message);
				// Remote Text Message
				wtkcall_perform_message_callback(&calls[e.ev.text.callNo].sm, e.ev.text.message);
			}
			else 
			{
				// Local Text message
				wtkcall_perform_text_callback(e.ev.text.message);
			}
		break; 

		default:
			iaxci_usermsg(IAXC_NOTICE, "wtkcall_iax_event_callback==>WTK uncared or unknow state:\t%d\n", e.type );
		break;
	}
	return 1;
}
int wtkcall_send_audio_callback(char* data, int len)
{
	int rtp_samples;
	int send_len = 0;
	
	rtp_samples = 40*(48000/1000);
	send_len = iaxc_push_audio(data, len, rtp_samples);
	iaxci_usermsg(IAXC_NOTICE, "%s!,size = %d,send_len = %d", __FUNCTION__, len,send_len);
	return send_len;
}
int wtkcall_send_video_callback(char* data, int len)
{
	iaxci_usermsg(IAXC_NOTICE, "%s!", __FUNCTION__, len);

	int send_len = 0;
	//send_len = iaxc_push_video(data, len, 0);
	return send_len;
}

//Extern API for SDK or APP
int wtkcall_initialize_iax(void)
{
	int retval = SUCESS_RET;
	
	char ver[32] = {0};
	iaxc_set_event_callback(wtkcall_iax_event_callback); 

	iaxc_version(ver);
	iaxci_usermsg(IAXC_NOTICE, "IAX2 Client Version is %s!", ver);
	
	// Initialize IAX Library
	if( iaxc_initialize( MAX_CALLS ) ) 
	{
		iaxci_usermsg(IAXC_ERROR, "Cannot initialize iaxclient!");
		retval = FAILED_RET;
		return retval;
	}
	
	retval = iaxc_start_processing_thread();
	if(retval == SUCESS_RET)
		iaxci_usermsg(IAXC_NOTICE, "IAX2 Client Initialized OK");
	
	return retval;
}

int wtkcall_initialize_media(void)
{
	int retval = SUCESS_RET;
	
	retval = libwtk_initialize();

	if(!retval)
	{
		libwtk_set_audio_transport(wtkcall_send_audio_callback);
		libwtk_set_video_transport(wtkcall_send_video_callback);
	}
	else
	{
		iaxci_usermsg(IAXC_ERROR, "Cannot initialize Wtk rtc engine!");
		iaxc_shutdown();
		retval = FAILED_RET;
		return retval;
	}
	
	retval = libwtk_init_audio_device(0);

	if(!retval) {
		libwtk_init_call();
		//libwtk_init_local_render();
		//libwtk_init_remote_render();
		//libwtk_init_capture(0);libwtk_decode_audio

		libwtk_create_audio_send_stream(0);
		libwtk_create_audio_receive_stream(0);
		libwtk_start_audio_stream();
		//libwtk_create_video_send_stream();
	}else{
		iaxci_usermsg(IAXC_ERROR,"Wtk rtc engine: cannot initialize audio device!\n");
		iaxc_shutdown();
		retval = FAILED_RET;
	}

	return retval;
}
void wtkcall_shutdown_iax(void)
{
	iaxc_stop_processing_thread();
	iaxc_shutdown(); 
	return;
}
void wtkcall_shutdown_media(void)
{

	return;
}

int wtkcall_register(const char* name,const char *number,const char *pass,const char *host,const char *port)
{
    char temp[256] = {0};
    char *pwd = NULL;
	int i;

	if(calls)
	{
		for(i=0; i<max_calls; i++) 
		{
			strncpy(calls[i].callerid_name, name, IAXC_EVENT_BUFSIZ);
			strncpy(calls[i].callerid_number, number, IAXC_EVENT_BUFSIZ);
		}
	}

    if(pass && strchr(pass, ':'))
    {
        strncpy(temp, pass, sizeof(temp));
        pwd = strtok(temp, ":");
    }
    else {
        pwd = pass;
    }

	return iaxc_register( number, pass, host );
}
void wtkcall_unregister( int id )
{
	int count = 0;
	get_iaxc_lock();
	count = iaxc_remove_registration_by_id(id);
	put_iaxc_lock();
	return;
}
int wtkcall_dial(const char* dest,const char* host,const char* user,const char *cmd,const char* ext)
{
	int /*callId = -1, */callNo = -1;
    uint64_t preferred_audio_format = 0;
	struct iax_session *newsession;

    char *name, *option, *secret, *pwd;
    char tmp[256] = {0};
    
	get_iaxc_lock();

	// if no call is selected, get a new appearance
	if ( selected_call < 0 )
	{
		callNo = iaxc_first_free_call();
	} 
	else
	{
		// use selected call if not active, otherwise, get a new appearance
		if ( calls[selected_call].state & IAXC_CALL_STATE_ACTIVE )
		{
			callNo = iaxc_first_free_call();
		} else
		{
			callNo = selected_call;
		}
	}

	if ( callNo < 0 )
	{
		iaxci_usermsg(IAXC_STATUS, "No free call appearances");
		goto iaxc_call_bail;
	}

	newsession = iax_session_new();
	if ( !newsession )
	{
		iaxci_usermsg(IAXC_ERROR, "Can't make new session");
		goto iaxc_call_bail;
	}
	
	calls[callNo].session = newsession;
	
	/* When the ACCEPT comes back from the other-end, these formats
	 * are set. Whether the format is set or not determines whether
	 * we are in the Linked state (see the iax2 rfc).
	 * These will have already been cleared by iaxc_clear_call(),
	 * but we reset them anyway just to be pedantic.
	 */
	calls[callNo].format = 0;
	calls[callNo].vformat = 0;

	/* We start by parsing up the temporary variable which is of the form of: 
	    [user@]peer[:portno][/exten[@context]] */
	if ( dest )
	{
		sprintf(calls[callNo].remote_name, "%s@%s/%s", user, host, dest);
		strncpy(calls[callNo].remote, dest, IAXC_EVENT_BUFSIZ);
	} else
	{
		sprintf(calls[callNo].remote_name, "%s@%s",user, host);
		strncpy(calls[callNo].remote,      "" , IAXC_EVENT_BUFSIZ);
	}

	if( cmd==NULL || strcmp(cmd, "")==0)
		strncpy(calls[callNo].local, calls[callNo].callerid_name, IAXC_EVENT_BUFSIZ);
	else 
	{
		strncpy(tmp, cmd, sizeof(tmp));
		
		if(cmd[0]=='/')
		{
			// there is no customized user name, use the caller name instead
			strncpy(calls[callNo].local, calls[callNo].callerid_name, IAXC_EVENT_BUFSIZ);
			option = strtok(tmp, "/");
		}
		else 
		{
			name = strtok(tmp, "/");
			if(name!=NULL)
				strncpy(calls[callNo].local, name, IAXC_EVENT_BUFSIZ);
			option = strtok(NULL, "/");
		}

		/*while (option!=NULL) 
		{
			if(strcmp(option, "mixer")==0) {
				calls[callNo].mstate |= IAXC_MEDIA_STATE_MIXED;
			}
			else if(strcmp(option, "nortp")==0) {
				calls[callNo].mstate |= IAXC_MEDIA_STATE_NORTP;
			}
			else if(strcmp(option, "forward")==0) {
				calls[callNo].mstate |= IAXC_MEDIA_STATE_FORWARD;
			}
			option = strtok(NULL, "/");
		}*/
	}

    strncpy(tmp, user, sizeof(tmp));
    if (strchr(tmp, ':')) {
		name = strtok(tmp, ":");
		secret = strtok(NULL, ":");
        
        if(secret && strchr(secret, ':')) {
            strncpy(tmp, secret, sizeof(tmp));
            pwd = strtok(tmp, ":");
        }
        else {
            pwd = secret;
        }
    }

	strncpy(calls[callNo].local_context, "default", IAXC_EVENT_BUFSIZ);

	calls[callNo].state = IAXC_CALL_STATE_OUTGOING |IAXC_CALL_STATE_ACTIVE;
	calls[callNo].last_ping = calls[callNo].last_activity;
	
	/* create a new identifier for the call */
	//callId = get_new_callid();
	//calls[callNo].sm.id = callId;
    //calls[callNo].sm.outbound = is_outbound(calls[callNo].remote);
	
	/* reset activity and ping "timers" */
	iaxc_note_activity(callNo);
	iaxci_usermsg(IAXC_NOTICE, "Originating an %s call", video_format_preferred ? "audio+video" : "audio only");

	iax_call(calls[callNo].session, calls[callNo].callerid_number,
		calls[callNo].local, user, host, dest, NULL, 0,
		audio_format_preferred | video_format_preferred, 
		audio_format_capability | video_format_capability,
        ext);

	iaxc_select_call(callNo);
    
iaxc_call_bail:
    
	put_iaxc_lock();
	return callNo;
}
void wtkcall_answer( int callNo )
{
    get_iaxc_lock();

	iaxci_usermsg(IAXC_NOTICE, "wtkcall_answer callNo = %d", callNo);
	if ( callNo >= 0 && callNo < max_calls ) {
        iaxc_answer_call( callNo );
    }
	put_iaxc_lock();
}
void wtkcall_select(int callNo)
{
	get_iaxc_lock();
	if( callNo >= 0 && callNo < max_calls) {
        iaxc_select_call(callNo);
    }
	put_iaxc_lock();
}

int wtkcall_hangup( int callNo)
{
	int ret = -1;
    
    get_iaxc_lock();
	if( callNo >= 0 && callNo < max_calls )
    {
        calls[callNo].sm.hangup |= HANGUP_SELF; //calls[callNo].sm.self = 1;
        
        if( (calls[callNo].state & IAXC_CALL_STATE_COMPLETE) ||
            (calls[callNo].state & IAXC_CALL_STATE_OUTGOING) )
        {
            iaxc_dump_one_call(callNo);  // hangup an outgoing call or answered call
        }
        else
        {
            iax_reject(calls[callNo].session, "Call rejected manually.");  // reject an incoming call
            iaxc_clear_call(callNo);
        }
        ret = 0;
    }
	put_iaxc_lock();
	return ret;
}
int wtkcall_hold(int callNo, bool hold)
{
	int ret = -1;
	get_iaxc_lock();
	if(callNo >= 0 && callNo < max_calls)
    {
		ret = 0;

		libwtk_audio_stream_mute(hold);

        if(!hold)
        {
            iaxc_select_call(callNo);
            iaxc_unquelch(callNo);
            
            if( calls[callNo].state & IAXC_CALL_STATE_ACTIVE &&
               calls[callNo].state & IAXC_CALL_STATE_COMPLETE ) {
                iax_send_text(calls[callNo].session, COMMAND_RESUME, 0);
            }
        }
        else
        {
            if( calls[callNo].state & IAXC_CALL_STATE_ACTIVE &&
               calls[callNo].state & IAXC_CALL_STATE_COMPLETE )
            {
                iax_send_text(calls[callNo].session, COMMAND_HOLD, 0);
            }
            
            iaxc_quelch(callNo, HOLD);
        }
    }
	put_iaxc_lock();
    return ret;
}
int wtkcall_mute(int callNo, bool mute)
{
	int ret = -1;

    get_iaxc_lock();
    if( callNo >= 0 && callNo < max_calls )
    {
    	ret = 0;
        libwtk_audio_stream_mute(mute);
		
		if(mute)  
			calls[callNo].mstate |= IAXC_MEDIA_STATE_MUTE;  // call muted
        else
			calls[callNo].mstate &= ~IAXC_MEDIA_STATE_MUTE; // call unmuted
    }
    put_iaxc_lock();

    return ret;
}
int wtkcall_set_format( int callNo, int rtp_format)
{
	if(callNo < 0 || callNo >= max_calls)
		return -1;

	if(!(calls[callNo].state & IAXC_CALL_STATE_COMPLETE) )
		return -2;
	
	/* Here we only set the RTP format of voice engine */
	//calls[callNo].rtp_format = rtp_format;
	
	/* Change the codec of VoiceEngine */
	libwtk_set_audio_codec();

	return 0;
}


