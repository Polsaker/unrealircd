/*
 *   IRC - Internet Relay Chat, src/modules/sjoin.c
 *   (C) 2004 The UnrealIRCd Team
 *
 *   See file AUTHORS in IRC package for additional names of
 *   the programmers.
 *
 *   This program is free software; you can redistribute it and/or modify
 *   it under the terms of the GNU General Public License as published by
 *   the Free Software Foundation; either version 1, or (at your option)
 *   any later version.
 *
 *   This program is distributed in the hope that it will be useful,
 *   but WITHOUT ANY WARRANTY; without even the implied warranty of
 *   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *   GNU General Public License for more details.
 *
 *   You should have received a copy of the GNU General Public License
 *   along with this program; if not, write to the Free Software
 *   Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
 */

#include "unrealircd.h"

CMD_FUNC(cmd_sjoin);

#define MSG_SJOIN 	"SJOIN"	

ModuleHeader MOD_HEADER
  = {
	"sjoin",
	"5.1",
	"command /sjoin", 
	"UnrealIRCd Team",
	"unrealircd-6",
    };

MOD_INIT()
{
	CommandAdd(modinfo->handle, MSG_SJOIN, cmd_sjoin, MAXPARA, CMD_SERVER);
	MARK_AS_OFFICIAL_MODULE(modinfo);
	return MOD_SUCCESS;
}

MOD_LOAD()
{
	return MOD_SUCCESS;
}

MOD_UNLOAD()
{
	return MOD_SUCCESS;
}

typedef struct xParv aParv;
struct xParv {
	int  parc;
	char *parv[256];
};

aParv pparv;

aParv *mp2parv(char *xmbuf, char *parmbuf)
{
	int  c;
	char *p, *s;

	pparv.parv[0] = xmbuf;
	c = 1;
	
	for (s = strtoken(&p, parmbuf, " "); s; s = strtoken(&p, NULL, " "))
	{
		pparv.parv[c] = s;
		c++; /* in my dreams */
	}
	pparv.parv[c] = NULL;
	pparv.parc = c;
	return (&pparv);
}

void send_local_chan_mode(MessageTag *recv_mtags, Client *client, Channel *channel, char *modebuf, char *parabuf)
{
	MessageTag *mtags = NULL;

	new_message_special(client, recv_mtags, &mtags, ":%s MODE %s %s %s", client->name, channel->name, modebuf, parabuf);
	sendto_channel(channel, client, NULL, 0, 0, SEND_LOCAL, mtags,
	               ":%s MODE %s %s %s", client->name, channel->name, modebuf, parabuf);
	if (MyConnect(client))
		RunHook(HOOKTYPE_LOCAL_CHANMODE, client, channel, mtags, modebuf, parabuf, 0, -1);
	else
		RunHook(HOOKTYPE_REMOTE_CHANMODE, client, channel, mtags, modebuf, parabuf, 0, -1);
	free_message_tags(mtags);
}

/** SJOIN: Synchronize channel modes, +beI lists and users (server-to-server command)
 * Extensive technical documentation is available at:
 * https://www.unrealircd.org/docs/Server_protocol:SJOIN_command
 *
 *  parv[1] = channel timestamp
 *  parv[2] = channel name
 *
 *  if parc == 3:
 *  parv[3] = nick names + modes - all in one parameter
 *
 *  if parc == 4:
 *  parv[3] = channel modes
 *  parv[4] = nick names + modes - all in one parameter
 *
 *  if parc > 4:
 *  parv[3] = channel modes
 *  parv[4 to parc - 2] = mode parameters
 *  parv[parc - 1] = nick names + modes
 */

/* Note: with regards to message tags we use new_message_special()
 *       here extensively. This because one SJOIN command can (often)
 *       generate multiple events that are sent to clients,
 *       for example 1 SJOIN can cause multiple joins, +beI, etc.
 *       -- Syzop
 */

/* Some ugly macros, but useful */
#define Addit(mode,param) if ((strlen(parabuf) + strlen(param) + 11 < MODEBUFLEN) && (b <= MAXMODEPARAMS)) { \
	if (*parabuf) \
		strcat(parabuf, " ");\
	strcat(parabuf, param);\
	modebuf[b++] = mode;\
	modebuf[b] = 0;\
}\
else {\
	send_local_chan_mode(recv_mtags, client, channel, modebuf, parabuf); \
	strcpy(parabuf,param);\
	/* modebuf[0] should stay what it was ('+' or '-') */ \
	modebuf[1] = mode;\
	modebuf[2] = '\0';\
	b = 2;\
}
#define Addsingle(x) do { modebuf[b] = x; b++; modebuf[b] = '\0'; } while(0)
#define CheckStatus(x,y) do { if (modeflags & (y)) { Addit((x), acptr->name); } } while(0)

CMD_FUNC(cmd_sjoin)
{
	unsigned short nopara;
	unsigned short nomode; /**< An SJOIN without MODE? */
	unsigned short removeours; /**< Remove our modes */
	unsigned short removetheirs; /**< Remove their modes (or actually: do not ADD their modes, the MODE -... line will be sent later by the other side) */
	unsigned short merge;	/**< same timestamp: merge their & our modes */
	char pvar[MAXMODEPARAMS][MODEBUFLEN + 3];
	char cbuf[1024];
	char nick[1024]; /**< nick or ban/invex/exempt being processed */
	char scratch_buf[1024]; /**< scratch buffer */
	char prefix[16]; /**< prefix of nick for server to server traffic (eg: @) */
	char uid_buf[BUFSIZE];  /**< Buffer for server-to-server traffic which will be broadcasted to others (servers supporting SID/UID) */
	char uid_sjsby_buf[BUFSIZE];  /**< Buffer for server-to-server traffic which will be broadcasted to others (servers supporting SID/UID and SJSBY) */
	char sj3_parabuf[BUFSIZE]; /**< Prefix for the above SJOIN buffers (":xxx SJOIN #channel +mode :") */
	char *s = NULL;
	Channel *channel; /**< Channel */
	aParv *ap;
	int pcount, i;
	Hook *h;
	Cmode *cm;
	time_t ts, oldts;
	unsigned short b=0;
	char *tp, *p, *saved = NULL;
	long modeflags;
	
	if (!IsServer(client) || parc < 4)
		return;

	if (!IsChannelName(parv[2]))
		return;

	merge = nopara = nomode = removeours = removetheirs = 0;

	if (parc < 6)
		nopara = 1;

	if (parc < 5)
		nomode = 1;

	channel = find_channel(parv[2]);
	if (!channel)
	{
		channel = make_channel(parv[2]);
		oldts = -1;
	}

	ts = (time_t)atol(parv[1]);

	if (IsInvalidChannelTS(ts))
	{
		unreal_log(ULOG_WARNING, "sjoin", "SJOIN_INVALID_TIMESTAMP", NULL,
			   "SJOIN for channel $channel has invalid timestamp $send_timestamp (from $client)",
			   log_data_channel("channel", channel),
			   log_data_integer("send_timestamp", ts));
		/* Pretend they match our creation time (matches U6 behavior in m_mode.c) */
		ts = channel->creationtime;
	}

	if (channel->creationtime > ts)
	{
		removeours = 1;
		oldts = channel->creationtime;
		channel->creationtime = ts;
	}
	else if ((channel->creationtime < ts) && (channel->creationtime != 0))
	{
		removetheirs = 1;
	}
	else if (channel->creationtime == ts)
	{
		merge = 1;
	}

	if (channel->creationtime > 0)
	{
		oldts = channel->creationtime;
	}

	parabuf[0] = '\0';
	modebuf[0] = '+';
	modebuf[1] = '\0';

	/* Grab current modes -> modebuf & parabuf */
	channel_modes(client, modebuf, parabuf, sizeof(modebuf), sizeof(parabuf), channel, 1);

	/* Do we need to remove all our modes, bans/exempt/inves lists and -vhoaq our users? */
	if (removeours)
	{
		Member *lp;
		Membership *lp2;

		modebuf[0] = '-';

		/* remove our modes if any */
		if (!empty_mode(modebuf))
		{
			MessageTag *mtags = NULL;
			ap = mp2parv(modebuf, parabuf);
			set_mode(channel, client, ap->parc, ap->parv, &pcount, pvar);
			send_local_chan_mode(recv_mtags, client, channel, modebuf, parabuf);
		}
		/* remove bans */
		/* reset the buffers */
		modebuf[0] = '-';
		modebuf[1] = '\0';
		parabuf[0] = '\0';
		b = 1;
		while(channel->banlist)
		{
			Ban *ban = channel->banlist;
			Addit('b', ban->banstr);
			channel->banlist = ban->next;
			safe_free(ban->banstr);
			safe_free(ban->who);
			free_ban(ban);
		}
		while(channel->exlist)
		{
			Ban *ban = channel->exlist;
			Addit('e', ban->banstr);
			channel->exlist = ban->next;
			safe_free(ban->banstr);
			safe_free(ban->who);
			free_ban(ban);
		}
		while(channel->invexlist)
		{
			Ban *ban = channel->invexlist;
			Addit('I', ban->banstr);
			channel->invexlist = ban->next;
			safe_free(ban->banstr);
			safe_free(ban->who);
			free_ban(ban);
		}
		for (lp = channel->members; lp; lp = lp->next)
		{
			lp2 = find_membership_link(lp->client->user->channel, channel);
			if (!lp2)
			{
				sendto_realops("Oops! channel->members && !find_membership_link");
				continue;
			}
			if (lp->flags & MODE_CHANOWNER)
			{
				lp->flags &= ~MODE_CHANOWNER;
				Addit('q', lp->client->name);
			}
			if (lp->flags & MODE_CHANADMIN)
			{
				lp->flags &= ~MODE_CHANADMIN;
				Addit('a', lp->client->name);
			}
			if (lp->flags & MODE_CHANOP)
			{
				lp->flags &= ~MODE_CHANOP;
				Addit('o', lp->client->name);
			}
			if (lp->flags & MODE_HALFOP)
			{
				lp->flags &= ~MODE_HALFOP;
				Addit('h', lp->client->name);
			}
			if (lp->flags & MODE_VOICE)
			{
				lp->flags &= ~MODE_VOICE;
				Addit('v', lp->client->name);
			}
			/* Those should always match anyways  */
			lp2->flags = lp->flags;
		}
		if (b > 1)
		{
			modebuf[b] = '\0';
			send_local_chan_mode(recv_mtags, client, channel, modebuf, parabuf);
		}

		/* since we're dropping our modes, we want to clear the mlock as well. --nenolod */
		set_channel_mlock(client, channel, NULL, FALSE);
	}
	/* Mode setting done :), now for our beloved clients */
	parabuf[0] = 0;
	modebuf[0] = '+';
	modebuf[1] = '\0';
	b = 1;
	strlcpy(cbuf, parv[parc-1], sizeof cbuf);

	sj3_parabuf[0] = '\0';
	for (i = 2; i <= (parc - 2); i++)
	{
		if (!parv[i])
		{
			sendto_ops("Got null parv in SJ3 code");
			continue;
		}
		strlcat(sj3_parabuf, parv[i], sizeof sj3_parabuf);
		if (((i + 1) <= (parc - 2)))
			strlcat(sj3_parabuf, " ", sizeof sj3_parabuf);
	}

	/* Now process adding of users & adding of list modes (bans/exempt/invex) */

	snprintf(uid_buf, sizeof uid_buf, ":%s SJOIN %lld %s :", client->id, (long long)ts, sj3_parabuf);
	snprintf(uid_sjsby_buf, sizeof uid_sjsby_buf, ":%s SJOIN %lld %s :", client->id, (long long)ts, sj3_parabuf);

	for (s = strtoken(&saved, cbuf, " "); s; s = strtoken(&saved, NULL, " "))
	{
		char *setby = client->name; /**< Set by (nick, nick!user@host, or server name) */
		time_t setat = TStime(); /**< Set at timestamp */
		int sjsby_info = 0; /**< Set to 1 if we receive SJSBY info to alter the above 2 vars */

		modeflags = 0;
		i = 0;
		tp = s;

		/* UnrealIRCd 4.2.2 and later support "SJSBY" which allows communicating
		 * setat/setby information for bans, ban exempts and invite exceptions.
		 */
		if (SupportSJSBY(client->direction) && (*tp == '<'))
		{
			/* Special prefix to communicate timestamp and setter:
			 * "<" + timestamp + "," + nick[!user@host] + ">" + normal SJOIN stuff
			 * For example: "<12345,nick>&some!nice@ban"
			 */
			char *end = strchr(tp, '>'), *p;
			if (!end)
			{
				/* this obviously should never happen */
				sendto_ops("Malformed SJOIN piece from %s for channel %s: %s",
					client->name, channel->name, tp);
				continue;
			}
			*end++ = '\0';

			p = strchr(tp, ',');
			if (!p)
			{
				/* missing setby parameter */
				sendto_ops("Malformed SJOIN piece from %s for channel %s: %s",
					client->name, channel->name, tp);
				continue;
			}
			*p++ = '\0';

			setat = atol(tp+1);
			setby = p;
			sjsby_info = 1;

			tp = end; /* the remainder is used for the actual ban/exempt/invex */
		}

		while (
		    (*tp == '@') || (*tp == '+') || (*tp == '%')
		    || (*tp == '*') || (*tp == '~') || (*tp == '&')
		    || (*tp == '"') || (*tp == '\''))
		{
			switch (*(tp++))
			{
			  case '@':
				  modeflags |= CHFL_CHANOP;
				  break;
			  case '%':
				  modeflags |= CHFL_HALFOP;
				  break;
			  case '+':
				  modeflags |= CHFL_VOICE;
				  break;
			  case '*':
				  modeflags |= CHFL_CHANOWNER;
				  break;
			  case '~':
				  modeflags |= CHFL_CHANADMIN;
				  break;
			  case '&':
				  modeflags = CHFL_BAN;
				  goto getnick;
			  case '"':
				  modeflags = CHFL_EXCEPT;
				  goto getnick;
			  case '\'':
				  modeflags = CHFL_INVEX;
				  goto getnick;
			}
		}
getnick:

		/* First, set the appropriate prefix for server to server traffic.
		 * Note that 'prefix' is a 16 byte buffer but it's safe due to the limited
		 * number of choices as can be seen below:
		 */
		*prefix = '\0';
		p = prefix;
		if (modeflags == CHFL_INVEX)
			*p++ = '\'';
		else if (modeflags == CHFL_EXCEPT)
			*p++ = '\"';
		else if (modeflags == CHFL_BAN)
			*p++ = '&';
		else
		{
			/* multiple options possible at the same time */
			if (modeflags & CHFL_CHANOWNER)
				*p++ = '*';
			if (modeflags & CHFL_CHANADMIN)
				*p++ = '~';
			if (modeflags & CHFL_CHANOP)
				*p++ = '@';
			if (modeflags & CHFL_HALFOP)
				*p++ = '%';
			if (modeflags & CHFL_VOICE)
				*p++ = '+';
		}
		*p = '\0';

		/* Now copy the "nick" (which can actually be a ban/invex/exempt).
		 * There's no size checking here but nick is 1024 bytes and we
		 * have 512 bytes input max.
		 */
		i = 0;
		while ((*tp != ' ') && (*tp != '\0'))
			nick[i++] = *(tp++);	/* get nick */
		nick[i] = '\0';
		if (nick[0] == ' ')
			continue;
		if (nick[0] == '\0')
			continue;
		if (!(modeflags & CHFL_BAN) && !(modeflags & CHFL_EXCEPT) && !(modeflags & CHFL_INVEX))
		{
			Client *acptr;

			/* A person joining */

			/* The user may no longer exist. This can happen in case of a
			 * SVSKILL traveling in the other direction. Nothing to worry about.
			 */
			if (!(acptr = find_person(nick, NULL)))
				continue;

			if (acptr->direction != client->direction)
			{
				if (IsMember(acptr, channel))
				{
					/* Nick collision, don't kick or it desyncs -Griever*/
					continue;
				}
			
				sendto_one(client, NULL,
				    ":%s KICK %s %s :Fake direction",
				    me.id, channel->name, acptr->name);
				sendto_realops
				    ("Fake direction from user %s in SJOIN from %s(%s) at %s",
				    nick, client->uplink->name,
				    client->name, channel->name);
				continue;
			}

			if (removetheirs)
			{
				modeflags = 0;
			}

			if (!IsMember(acptr, channel))
			{
				/* User joining the channel, send JOIN to local users.
				 */
				MessageTag *mtags = NULL;

				add_user_to_channel(channel, acptr, modeflags);
				RunHook(HOOKTYPE_REMOTE_JOIN, acptr, channel, recv_mtags);
				new_message_special(acptr, recv_mtags, &mtags, ":%s JOIN %s", acptr->name, channel->name);
				send_join_to_local_users(acptr, channel, mtags);
				free_message_tags(mtags);
			}

			CheckStatus('q', CHFL_CHANOWNER);
			CheckStatus('a', CHFL_CHANADMIN);
			CheckStatus('o', CHFL_CHANOP);
			CheckStatus('h', CHFL_HALFOP);
			CheckStatus('v', CHFL_VOICE);

			if (strlen(uid_buf) + strlen(prefix) + IDLEN > BUFSIZE - 10)
			{
				/* Send what we have and start a new buffer */
				sendto_server(client, 0, PROTO_SJSBY, recv_mtags, "%s", uid_buf);
				snprintf(uid_buf, sizeof(uid_buf), ":%s SJOIN %lld %s :", client->id, (long long)ts, channel->name);
				/* Double-check the new buffer is sufficient to concat the data */
				if (strlen(uid_buf) + strlen(prefix) + strlen(acptr->id) > BUFSIZE - 5)
				{
					unreal_log(ULOG_ERROR, "sjoin", "BUG_OVERSIZED_SJOIN", client,
					           "Oversized SJOIN [$sjoin_place] in channel $channel when adding '$str$str2' to '$buf'",
					           log_data_string("sjoin_place", "UID-MEMBER"),
					           log_data_string("str", prefix),
					           log_data_string("str2", acptr->id),
					           log_data_string("buf", uid_buf));
					continue;
				}
			}
			sprintf(uid_buf+strlen(uid_buf), "%s%s ", prefix, acptr->id);

			if (strlen(uid_sjsby_buf) + strlen(prefix) + IDLEN > BUFSIZE - 10)
			{
				/* Send what we have and start a new buffer */
				sendto_server(client, PROTO_SJSBY, 0, recv_mtags, "%s", uid_sjsby_buf);
				snprintf(uid_sjsby_buf, sizeof(uid_sjsby_buf), ":%s SJOIN %lld %s :", client->id, (long long)ts, channel->name);
				/* Double-check the new buffer is sufficient to concat the data */
				if (strlen(uid_sjsby_buf) + strlen(prefix) + strlen(acptr->id) > BUFSIZE - 5)
				{
					unreal_log(ULOG_ERROR, "sjoin", "BUG_OVERSIZED_SJOIN", client,
					           "Oversized SJOIN [$sjoin_place] in channel $channel when adding '$str$str2' to '$buf'",
					           log_data_string("sjoin_place", "SJS-MEMBER"),
					           log_data_string("str", prefix),
					           log_data_string("str2", acptr->id),
					           log_data_string("buf", uid_sjsby_buf));
					continue;
				}
			}
			sprintf(uid_sjsby_buf+strlen(uid_sjsby_buf), "%s%s ", prefix, acptr->id);
		}
		else
		{
			if (removetheirs)
				continue;

			/* For list modes (beI): validate the syntax */
			if (modeflags & (CHFL_BAN|CHFL_EXCEPT|CHFL_INVEX))
			{
				const char *str;
				
				/* non-extbans: prevent bans without ! or @. a good case of "should never happen". */
				if ((nick[0] != '~') && (!strchr(nick, '!') || !strchr(nick, '@') || (nick[0] == '!')))
					continue;
				
				str = clean_ban_mask(nick, MODE_ADD, client, 0);
				if (!str)
					continue; /* invalid ban syntax */
				strlcpy(nick, str, sizeof(nick));
			}
			
			/* Adding of list modes */
			if (modeflags & CHFL_BAN)
			{
				if (add_listmode_ex(&channel->banlist, client, channel, nick, setby, setat) != -1)
				{
					Addit('b', nick);
				}
			}
			if (modeflags & CHFL_EXCEPT)
			{
				if (add_listmode_ex(&channel->exlist, client, channel, nick, setby, setat) != -1)
				{
					Addit('e', nick);
				}
			}
			if (modeflags & CHFL_INVEX)
			{
				if (add_listmode_ex(&channel->invexlist, client, channel, nick, setby, setat) != -1)
				{
					Addit('I', nick);
				}
			}

			if (strlen(uid_buf) + strlen(prefix) + strlen(nick) > BUFSIZE - 10)
			{
				/* Send what we have and start a new buffer */
				sendto_server(client, 0, PROTO_SJSBY, recv_mtags, "%s", uid_buf);
				snprintf(uid_buf, sizeof(uid_buf), ":%s SJOIN %lld %s :", client->id, (long long)ts, channel->name);
				/* Double-check the new buffer is sufficient to concat the data */
				if (strlen(uid_buf) + strlen(prefix) + strlen(nick) > BUFSIZE - 5)
				{
					unreal_log(ULOG_ERROR, "sjoin", "BUG_OVERSIZED_SJOIN", client,
					           "Oversized SJOIN [$sjoin_place] in channel $channel when adding '$str$str2' to '$buf'",
					           log_data_string("sjoin_place", "UID-LMODE"),
					           log_data_string("str", prefix),
					           log_data_string("str2", nick),
					           log_data_string("buf", uid_buf));
					continue;
				}
			}
			sprintf(uid_buf+strlen(uid_buf), "%s%s ", prefix, nick);

			*scratch_buf = '\0';
			if (sjsby_info)
				add_sjsby(scratch_buf, setby, setat);
			strcat(scratch_buf, prefix);
			strcat(scratch_buf, nick);
			strcat(scratch_buf, " ");
			if (strlen(uid_sjsby_buf) + strlen(scratch_buf) > BUFSIZE - 10)
			{
				/* Send what we have and start a new buffer */
				sendto_server(client, PROTO_SJSBY, 0, recv_mtags, "%s", uid_sjsby_buf);
				snprintf(uid_sjsby_buf, sizeof(uid_sjsby_buf), ":%s SJOIN %lld %s :", client->id, (long long)ts, channel->name);
				/* Double-check the new buffer is sufficient to concat the data */
				if (strlen(uid_sjsby_buf) + strlen(scratch_buf) > BUFSIZE - 5)
				{
					unreal_log(ULOG_ERROR, "sjoin", "BUG_OVERSIZED_SJOIN", client,
					           "Oversized SJOIN [$sjoin_place] in channel $channel when adding '$str$str2' to '$buf'",
					           log_data_string("sjoin_place", "SJS-LMODE"),
					           log_data_string("str", scratch_buf),
					           log_data_string("buf", uid_sjsby_buf));
					continue;
				}
			}
			strcpy(uid_sjsby_buf+strlen(uid_sjsby_buf), scratch_buf); /* size already checked above */
		}
		continue;
	}

	/* Send out any possible remainder.. */
	sendto_server(client, 0, PROTO_SJSBY, recv_mtags, "%s", uid_buf);
	sendto_server(client, PROTO_SJSBY, 0, recv_mtags, "%s", uid_sjsby_buf);

	if (!empty_mode(modebuf))
	{
		modebuf[b] = '\0';
		send_local_chan_mode(recv_mtags, client, channel, modebuf, parabuf);
	}
	
	if (!merge && !removetheirs && !nomode)
	{
		char paraback[1024];
		MessageTag *mtags = NULL;

		strlcpy(modebuf, parv[3], sizeof modebuf);
		parabuf[0] = '\0';
		if (!nopara)
		{
			for (b = 4; b <= (parc - 2); b++)
			{
				strlcat(parabuf, parv[b], sizeof parabuf);
				strlcat(parabuf, " ", sizeof parabuf);
			}
		}
		strlcpy(paraback, parabuf, sizeof paraback);
		ap = mp2parv(modebuf, parabuf);

		set_mode(channel, client, ap->parc, ap->parv, &pcount, pvar);
		send_local_chan_mode(recv_mtags, client, channel, modebuf, parabuf);
	}

	if (merge && !nomode)
	{
		CoreChannelModeTable *acp;
		Mode oldmode; /**< The old mode (OUR mode) */

		/* Copy current mode to oldmode (need to duplicate all extended mode params too..) */
		memcpy(&oldmode, &channel->mode, sizeof(oldmode));
		memset(&oldmode.mode_params, 0, sizeof(oldmode.mode_params));
		extcmode_duplicate_paramlist(channel->mode.mode_params, oldmode.mode_params);

		/* Now merge the modes */
		strlcpy(modebuf, parv[3], sizeof modebuf);
		parabuf[0] = '\0';
		if (!nopara)
		{
			for (b = 4; b <= (parc - 2); b++)
			{
				strlcat(parabuf, parv[b], sizeof parabuf);
				strlcat(parabuf, " ", sizeof parabuf);
			}
		}
		ap = mp2parv(modebuf, parabuf);
		set_mode(channel, client, ap->parc, ap->parv, &pcount, pvar);

		/* Good, now we got modes, now for the differencing and outputting of modes
		 * We first see if any para modes are set.
		 */
		strlcpy(modebuf, "-", sizeof modebuf);
		parabuf[0] = '\0';
		b = 1;

		/* First, check if we had something that is now gone
		 * note that: oldmode.* = us, channel->mode.* = merged.
		 */
		for (cm=channelmodes; cm; cm = cm->next)
		{
			if (cm->letter &&
			    !cm->local &&
			    (oldmode.mode & cm->mode) &&
			    !(channel->mode.mode & cm->mode))
			{
				if (cm->paracount)
				{
					const char *parax = cm_getparameter_ex(oldmode.mode_params, cm->letter);
					//char *parax = cm->get_param(extcmode_get_struct(oldmode.modeparam, cm->letter));
					Addit(cm->letter, parax);
				} else {
					Addsingle(cm->letter);
				}
			}
		}

#if 0
		// FIXME: fix this case of +p/+s merging... which should end up in +s:
		// can use get_extmode_bitbychar() or shit but probably should call a hook (or sjoin thingy) instead?

		/* Check if we had +s and it became +p, then revert it... */
		if ((oldmode.mode & MODE_SECRET) && (channel->mode.mode & MODE_PRIVATE))
		{
			/* stay +s ! */
			channel->mode.mode &= ~MODE_PRIVATE;
			channel->mode.mode |= MODE_SECRET;
			Addsingle('p'); /* - */
			queue_s = 1;
		}
#endif
		if (b > 1)
		{
			Addsingle('+');
		}
		else
		{
			strlcpy(modebuf, "+", sizeof modebuf);
			b = 1;
		}

		/* Now, check if merged modes contain something we didn't have before.
		 * note that: oldmode.* = us before, channel->mode.* = merged.
		 *
		 * First the simple single letter modes...
		 */
		for (cm=channelmodes; cm; cm = cm->next)
		{
			if ((cm->letter) &&
			    !(oldmode.mode & cm->mode) &&
			    (channel->mode.mode & cm->mode))
			{
				if (cm->paracount)
				{
					const char *parax = cm_getparameter(channel, cm->letter);
					if (parax)
					{
						Addit(cm->letter, parax);
					}
				} else {
					Addsingle(cm->letter);
				}
			}
		}

		/* now, if we had diffent para modes - this loop really could be done better, but */

		/* Now, check for any param differences in extended channel modes..
		 * note that: oldmode.* = us before, channel->mode.* = merged.
		 * if we win: copy oldmode to channel mode, if they win: send the mode
		 */
		for (cm=channelmodes; cm; cm = cm->next)
		{
			if (cm->letter && cm->paracount &&
			    (oldmode.mode & cm->mode) &&
			    (channel->mode.mode & cm->mode))
			{
				int r;
				const char *parax;
				char flag = cm->letter;
				void *ourm = GETPARASTRUCTEX(oldmode.mode_params, flag);
				void *theirm = GETPARASTRUCT(channel, flag);
				
				r = cm->sjoin_check(channel, ourm, theirm);
				switch (r)
				{
					case EXSJ_WEWON:
						parax = cm_getparameter_ex(oldmode.mode_params, flag); /* grab from old */
						cm_putparameter(channel, flag, parax); /* put in new (won) */
						break;

					case EXSJ_THEYWON:
						parax = cm_getparameter(channel, flag);
						Addit(cm->letter, parax);
						break;

					case EXSJ_SAME:
						break;

					case EXSJ_MERGE:
						parax = cm_getparameter_ex(oldmode.mode_params, flag); /* grab from old */
						cm_putparameter(channel, flag, parax); /* put in new (won) */
						Addit(flag, parax);
						break;

					default:
						unreal_log(ULOG_ERROR, "sjoin", "BUG_SJOIN_CHECK", client,
						           "[BUG] channel.c:m_sjoin:param diff checker: unknown return value $return_value",
						           log_data_integer("return_value", r));
						break;
				}
			}
		}

		Addsingle('\0');

		if (!empty_mode(modebuf))
			send_local_chan_mode(recv_mtags, client, channel, modebuf, parabuf);

		/* free the oldmode.* crap :( */
		extcmode_free_paramlist(oldmode.mode_params);
	}

	for (h = Hooks[HOOKTYPE_CHANNEL_SYNCED]; h; h = h->next)
	{
		int i = (*(h->func.intfunc))(channel,merge,removetheirs,nomode);
		if (i == 1)
			return; /* channel no longer exists */
	}

	/* we should be synced by now, */
	if ((oldts != -1) && (oldts != channel->creationtime))
	{
		MessageTag *mtags = NULL;
		new_message(client, NULL, &mtags);
		sendto_channel(channel, &me, NULL, 0, 0, SEND_LOCAL, NULL,
			":%s NOTICE %s :*** TS for %s changed from %lld to %lld",
			me.name, channel->name, channel->name,
			(long long)oldts, (long long)channel->creationtime);
		free_message_tags(mtags);
	}

	/* If something went wrong with processing of the SJOIN above and
	 * the channel actually has no users in it at this point,
	 * then destroy the channel.
	 */
	if (!channel->users)
	{
		sub1_from_channel(channel);
		return;
	}
}
