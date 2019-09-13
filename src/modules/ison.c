/*
 *   IRC - Internet Relay Chat, src/modules/m_ison.c
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

CMD_FUNC(m_ison);

#define MSG_ISON 	"ISON"	

ModuleHeader MOD_HEADER
  = {
	"ison",
	"5.0",
	"command /ison", 
	"UnrealIRCd Team",
	"unrealircd-5",
    };

MOD_INIT()
{
	CommandAdd(modinfo->handle, MSG_ISON, m_ison, 1, M_USER);
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

/*
 * m_ison added by Darren Reed 13/8/91 to act as an efficent user indicator
 * with respect to cpu/bandwidth used. Implemented for NOTIFY feature in
 * clients. Designed to reduce number of whois requests. Can process
 * nicknames in batches as long as the maximum buffer length.
 *
 * format:
 * ISON :nicklist
 */

static char buf[BUFSIZE];

CMD_FUNC(m_ison)
{
	char namebuf[USERLEN + HOSTLEN + 4];
	Client *acptr;
	char *s, *user;
	char *p = NULL;

	if (!MyUser(sptr))
		return 0;

	if (parc < 2)
	{
		sendnumeric(sptr, ERR_NEEDMOREPARAMS, "ISON");
		return 0;
	}

	ircsnprintf(buf, sizeof(buf), rpl_str(RPL_ISON), me.name, sptr->name);

	for (s = strtoken(&p, parv[1], " "); s; s = strtoken(&p, NULL, " "))
	{
		if ((user = strchr(s, '!')))
			*user++ = '\0';
		if ((acptr = find_person(s, NULL)))
		{
			if (user)
			{
				ircsnprintf(namebuf, sizeof(namebuf), "%s@%s", acptr->user->username, GetHost(acptr));
				if (!match_simple(user, namebuf)) continue;
				*--user = '!';
			}

			strlcat(buf, s, sizeof(buf));
			strlcat(buf, " ", sizeof(buf));
		}
	}

	sendto_one(sptr, NULL, "%s", buf);
	return 0;
}
