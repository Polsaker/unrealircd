/*
 *   IRC - Internet Relay Chat, src/modules/m_samode.c
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

CMD_FUNC(m_samode);

#define MSG_SAMODE 	"SAMODE"	

ModuleHeader MOD_HEADER
  = {
	"samode",
	"5.0",
	"command /samode", 
	"UnrealIRCd Team",
	"unrealircd-5",
    };

MOD_INIT()
{
	CommandAdd(modinfo->handle, MSG_SAMODE, m_samode, MAXPARA, M_USER);
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
 * m_samode
 * parv[1] = channel
 * parv[2] = modes
 * -t
 */
CMD_FUNC(m_samode)
{
	Channel *chptr;

	if (parc <= 2)
	{
		sendnumeric(sptr, ERR_NEEDMOREPARAMS, "SAMODE");
		return 0;
	}

	chptr = find_channel(parv[1], NULL);
	if (!chptr)
	{
		sendnumeric(sptr, ERR_NOSUCHCHANNEL, parv[1]);
		return 0;
	}

	if (!ValidatePermissionsForPath("sacmd:samode", sptr, NULL, chptr, NULL))
	{
		sendnumeric(sptr, ERR_NOPRIVILEGES);
		return 0;
	}

	opermode = 0;
	(void)do_mode(chptr, cptr, sptr, NULL, parc - 2, parv + 2, 0, 1);

	return 0;
}
