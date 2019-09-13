/*
 * Disallow notices in channel UnrealIRCd Module (Channel Mode +T)
 * (C) Copyright 2014 Travis McArthur (Heero) and the UnrealIRCd team
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 1, or (at your option)
 * any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
 */

#include "unrealircd.h"

ModuleHeader MOD_HEADER
  = {
	"chanmodes/nonotice",
	"4.2",
	"Channel Mode +T",
	"UnrealIRCd Team",
	"unrealircd-5",
    };

Cmode_t EXTCMODE_NONOTICE;

#define IsNoNotice(chptr)    (chptr->mode.extmode & EXTCMODE_NONOTICE)

int nonotice_check_can_send(Client *cptr, Channel *chptr, Membership *lp, char **msg, char **errmsg, int notice);

MOD_TEST()
{
	return MOD_SUCCESS;
}

MOD_INIT()
{
	CmodeInfo req;

	memset(&req, 0, sizeof(req));
	req.paracount = 0;
	req.flag = 'T';
	req.is_ok = extcmode_default_requirechop;
	CmodeAdd(modinfo->handle, req, &EXTCMODE_NONOTICE);
	
	HookAdd(modinfo->handle, HOOKTYPE_CAN_SEND, 0, nonotice_check_can_send);

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

int nonotice_check_can_send(Client *cptr, Channel *chptr, Membership *lp, char **msg, char **errmsg, int notice)
{
	Hook *h;
	int i;

	if (notice && IsNoNotice(chptr) &&
	   (!lp || !(lp->flags & (CHFL_CHANOP | CHFL_CHANOWNER | CHFL_CHANADMIN))))
	{
		for (h = Hooks[HOOKTYPE_CAN_BYPASS_CHANNEL_MESSAGE_RESTRICTION]; h; h = h->next)
		{
			i = (*(h->func.intfunc))(cptr, chptr, BYPASS_CHANMSG_NOTICE);
			if (i == HOOK_ALLOW)
				return HOOK_CONTINUE; /* bypass restriction */
			if (i != HOOK_CONTINUE)
				break;
		}
		*errmsg = "NOTICEs are not permitted in this channel";
		return HOOK_DENY; /* block notice */
	}

	return HOOK_CONTINUE;
}
