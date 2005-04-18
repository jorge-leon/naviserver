/*
 * The contents of this file are subject to the AOLserver Public License
 * Version 1.1 (the "License"); you may not use this file except in
 * compliance with the License. You may obtain a copy of the License at
 * http://aolserver.com/.
 *
 * Software distributed under the License is distributed on an "AS IS"
 * basis, WITHOUT WARRANTY OF ANY KIND, either express or implied. See
 * the License for the specific language governing rights and limitations
 * under the License.
 *
 * The Original Code is AOLserver Code and related documentation
 * distributed by AOL.
 * 
 * The Initial Developer of the Original Code is America Online,
 * Inc. Portions created by AOL are Copyright (C) 1999 America Online,
 * Inc. All Rights Reserved.
 *
 * Alternatively, the contents of this file may be used under the terms
 * of the GNU General Public License (the "GPL"), in which case the
 * provisions of GPL are applicable instead of those above.  If you wish
 * to allow use of your version of this file only under the terms of the
 * GPL and not to allow others to use your version of this file under the
 * License, indicate your decision by deleting the provisions above and
 * replace them with the notice and other provisions required by the GPL.
 * If you do not delete the provisions above, a recipient may use your
 * version of this file under either the License or the GPL.
 */


/*
 * tclresp.c --
 *
 *	Tcl commands for returning data to the user agent. 
 */

static const char *RCSID = "@(#) $Header$, compiled: " __DATE__ " " __TIME__;

#include "nsd.h"

static int Result(Tcl_Interp *interp, int result);
static int GetConn(ClientData arg, Tcl_Interp *interp, Ns_Conn **connPtr);


/*
 *----------------------------------------------------------------------
 *
 * NsTclHeadersObjCmd --
 *
 *	Spit out initial HTTP response; this is for backwards 
 *	compatibility only. 
 *
 * Results:
 *	Tcl result. 
 *
 * Side effects:
 *	None. 
 *
 *----------------------------------------------------------------------
 */

int
NsTclHeadersObjCmd(ClientData arg, Tcl_Interp *interp, int objc, Tcl_Obj *CONST objv[])
{
    int      status, len = -1;
    Ns_Conn *conn;
    char    *type = NULL;

    if (objc < 2 || objc > 4) {
        Tcl_WrongNumArgs(interp, 1, objv, "status ?type? ?len?");
        return TCL_ERROR;
    }
    if (GetConn(arg, interp, &conn) != TCL_OK) {
	return TCL_ERROR;
    }
    if (Tcl_GetIntFromObj(interp, objv[1], &status) != TCL_OK) {
        return TCL_ERROR;
    }
    if (objc > 2) {
        type = Tcl_GetString(objv[2]);
    }
    if (objc > 3 && Tcl_GetIntFromObj(interp, objv[3], &len) != TCL_OK) {
        return TCL_ERROR;
    }
    Ns_ConnSetRequiredHeaders(conn, type, len);
    return Result(interp, Ns_ConnFlushHeaders(conn, status));
}


/*
 *----------------------------------------------------------------------
 *
 * NsTclReturnObjCmd --
 *
 *	Implements ns_return as obj command. 
 *
 * Results:
 *	Tcl result. 
 *
 * Side effects:
 *	See docs. 
 *
 *----------------------------------------------------------------------
 */

int
NsTclReturnObjCmd(ClientData arg, Tcl_Interp *interp, int objc, Tcl_Obj *CONST objv[])
{
    Ns_Conn *conn;
    int      status, result;

    if (objc != 4) {
        Tcl_WrongNumArgs(interp, 1, objv, "status type string");
        return TCL_ERROR;
    }
    if (GetConn(arg, interp, &conn) != TCL_OK) {
	return TCL_ERROR;
    }
    if (Tcl_GetIntFromObj(interp, objv[objc-3], &status) != TCL_OK) {
        return TCL_ERROR;
    }
    result = Ns_ConnReturnCharData(conn, status, Tcl_GetString(objv[objc-1]), -1, 
	    Tcl_GetString(objv[objc-2]));
    return Result(interp, result);
}


/*
 *----------------------------------------------------------------------
 *
 * NsTclRespondObjCmd --
 *
 *	Implements ns_respond as obj command. 
 *
 * Results:
 *	Tcl result. 
 *
 * Side effects:
 *	See ns_respond. 
 *
 *----------------------------------------------------------------------
 */

int
NsTclRespondObjCmd(ClientData arg, Tcl_Interp *interp, int objc, Tcl_Obj *CONST objv[])
{
    Ns_Conn     *conn;
    int          status = 200, length = -1;
    char        *type = "*/*", *setid = NULL;
    char        *string = NULL, *filename = NULL, *chanid = NULL;
    Ns_Set      *set = NULL;
    Tcl_Channel	 chan;
    int          retval;

    Ns_ObjvSpec opts[] = {
        {"-status",   Ns_ObjvInt,    &status,   NULL},
        {"-type",     Ns_ObjvString, &type,     NULL},
        {"-length",   Ns_ObjvInt,    &length,   NULL},
        {"-headers",  Ns_ObjvString, &setid,    NULL},
        {"-string",   Ns_ObjvString, &string,   NULL},
        {"-file",     Ns_ObjvString, &filename, NULL},
        {"-fileid",   Ns_ObjvString, &chanid,   NULL},
        {NULL, NULL, NULL, NULL}
    };
    if (Ns_ParseObjv(opts, NULL, interp, 1, objc, objv) != NS_OK) {
        return TCL_ERROR;
    }

    if (chanid != NULL && length < 0) {
        Tcl_SetResult(interp, "length required when -fileid is used", 
                      TCL_STATIC);
        return TCL_ERROR;
    }
    if ((string != NULL) + (filename != NULL) + (chanid != NULL) != 1) {
        Tcl_SetResult(interp, "must specify only one of -string, -file "
                      "or -fileid", TCL_STATIC);
        return TCL_ERROR;
    }
    if (setid != NULL) {
        set = Ns_TclGetSet(interp, setid);
	    if (set == NULL) {
            Ns_TclPrintfResult(interp, "illegal ns_set id: \"%s\"", setid);
            return TCL_ERROR;
	    }
    }
    if (GetConn(arg, interp, &conn) != TCL_OK) {
        return TCL_ERROR;
    }
    if (set != NULL) {
	    Ns_ConnReplaceHeaders(conn, set);
    }

    if (chanid != NULL) {
        /*
         * We'll be returning an open channel
         */

	    if (Ns_TclGetOpenChannel(interp, chanid, 0, 1, &chan) != TCL_OK) {
            return TCL_ERROR;
        }
        retval = Ns_ConnReturnOpenChannel(conn, status, type, chan, length);

    } else if (filename != NULL) {
        /*
         * We'll be returning a file by name
         */

        retval = Ns_ConnReturnFile(conn, status, type, filename);

    } else {
        /*
         * We'll be returning a string now.
         */

        retval = Ns_ConnReturnCharData(conn, status, string, length, type);
    }

    return Result(interp, retval);
}


/*
 *----------------------------------------------------------------------
 *
 * NsTclReturnFileObjCmd --
 *
 *	Return an open file. (ns_returnfile) 
 *
 * Results:
 *	Tcl result. 
 *
 * Side effects:
 *	See docs. 
 *
 *----------------------------------------------------------------------
 */

int
NsTclReturnFileObjCmd(ClientData arg, Tcl_Interp *interp, int objc, Tcl_Obj *CONST objv[])
{
    int      status;
    Ns_Conn *conn;

    if (objc != 4) {
        Tcl_WrongNumArgs(interp, 1, objv, "status type filename");
        return TCL_ERROR;
    }
    if (GetConn(arg, interp, &conn) != TCL_OK) {
        return TCL_ERROR;
    }
    if (Tcl_GetIntFromObj(interp, objv[1], &status) != TCL_OK) {
        return TCL_ERROR;
    }
    return Result(interp, Ns_ConnReturnFile(conn, status, Tcl_GetString(objv[2]), 
				Tcl_GetString(objv[3])));
}


/*
 *----------------------------------------------------------------------
 *
 * NsTclReturnFpObjCmd --
 *
 *	Implements ns_returnfp. (actually accepts any open channel)
 *
 * Results:
 *	Tcl result. 
 *
 * Side effects:
 *	See docs. 
 *
 *----------------------------------------------------------------------
 */

int
NsTclReturnFpObjCmd(ClientData arg, Tcl_Interp *interp, int objc, Tcl_Obj *CONST objv[])
{
    int          len, status;
    Tcl_Channel	 chan;
    Ns_Conn     *conn;

    if (objc != 5) {
        Tcl_WrongNumArgs(interp, 1, objv, "status type channel len");
        return TCL_ERROR;
    }
    if (GetConn(arg, interp, &conn) != TCL_OK) {
        return TCL_ERROR;
    }
    if (Tcl_GetIntFromObj(interp, objv[1], &status) != TCL_OK) {
        return TCL_ERROR;
    }
    if (Tcl_GetIntFromObj(interp, objv[4], &len) != TCL_OK) {
        return TCL_ERROR;
    }
    if (Ns_TclGetOpenChannel(interp, Tcl_GetString(objv[3]), 0, 1, &chan) != TCL_OK) {
        return TCL_ERROR;
    }
    return Result(interp, Ns_ConnReturnOpenChannel(conn, status, Tcl_GetString(objv[2]), chan, len));
}


/*
 *----------------------------------------------------------------------
 *
 * NsTclReturnBadRequestObjCmd --
 *
 *	Implements ns_returnbadrequest as obj command. 
 *
 * Results:
 *	Tcl result. 
 *
 * Side effects:
 *	See docs. 
 *
 *----------------------------------------------------------------------
 */

int
NsTclReturnBadRequestObjCmd(ClientData arg, Tcl_Interp *interp, int objc, Tcl_Obj *CONST objv[])
{
    Ns_Conn *conn;

    if (objc != 2) {
        Tcl_WrongNumArgs(interp, 1, objv, "reason");
        return TCL_ERROR;
    }
    if (GetConn(arg, interp, &conn) != TCL_OK) {
        return TCL_ERROR;
    }
    return Result(interp, Ns_ConnReturnBadRequest(conn, Tcl_GetString(objv[1])));
}


/*
 *----------------------------------------------------------------------
 *
 * NsTclSimpleReturnObjCmd --
 *
 *	A generic way of returning from tcl; this implements 
 *	ns_returnnotfound and ns_returnforbidden. It uses the 
 *	clientdata to know what to do. 
 *
 * Results:
 *	Tcl result. 
 *
 * Side effects:
 *	Will call proc that is clientdata. 
 *
 *----------------------------------------------------------------------
 */

static int
ReturnObjCmd(ClientData arg, Tcl_Interp *interp, int objc, 
		Tcl_Obj *CONST objv[], int (*proc) (Ns_Conn *))
{
    Ns_Conn *conn;

    if (GetConn(arg, interp, &conn) != TCL_OK) {
	return TCL_ERROR;
    }
    return Result(interp, (*proc)(conn));
}

int
NsTclReturnNotFoundObjCmd(ClientData arg, Tcl_Interp *interp, int objc, Tcl_Obj *CONST objv[])
{
    return ReturnObjCmd(arg, interp, objc, objv, Ns_ConnReturnNotFound);
}

int
NsTclReturnUnauthorizedObjCmd(ClientData arg, Tcl_Interp *interp, int objc, Tcl_Obj *CONST objv[])
{
    return ReturnObjCmd(arg, interp, objc, objv, Ns_ConnReturnUnauthorized);
}

int
NsTclReturnForbiddenObjCmd(ClientData arg, Tcl_Interp *interp, int objc, Tcl_Obj *CONST objv[])
{
    return ReturnObjCmd(arg, interp, objc, objv, Ns_ConnReturnForbidden);
}


/*
 *----------------------------------------------------------------------
 *
 * NsTclReturnErrorObjCmd --
 *
 *	Implements ns_returnerror as obj command.
 *
 * Results:
 *	Tcl result. 
 *
 * Side effects:
 *	See docs. 
 *
 *----------------------------------------------------------------------
 */

int
NsTclReturnErrorObjCmd(ClientData arg, Tcl_Interp *interp, int objc, Tcl_Obj *CONST objv[])
{
    int      status;
    Ns_Conn *conn;

    if (objc != 3) {
        Tcl_WrongNumArgs(interp, 1, objv, "status message");
        return TCL_ERROR;
    }
    if (GetConn(arg, interp, &conn) != TCL_OK) {
        return TCL_ERROR;
    }
    if (Tcl_GetIntFromObj(interp, objv[1], &status) != TCL_OK) {
        return TCL_ERROR;
    }
    return Result(interp,
        Ns_ConnReturnAdminNotice(conn, status, "Request Error", 
                                 Tcl_GetString(objv[2])));
}


/*
 *----------------------------------------------------------------------
 *
 * NsTclReturnNoticeObjCmd --
 *
 *      Implements ns_returnnotice command.
 *
 * Results:
 *	Tcl result. 
 *
 * Side effects:
 *	See docs. 
 *
 *----------------------------------------------------------------------
 */

int
NsTclReturnNoticeObjCmd(ClientData arg, Tcl_Interp *interp, int objc, Tcl_Obj *CONST objv[])
{
    Ns_Conn *conn;
    int      status;

    if (objc != 4) {
        Tcl_WrongNumArgs(interp, 1, objv, "status title message");
        return TCL_ERROR;
    }
    if (GetConn(arg, interp, &conn) != TCL_OK) {
        return TCL_ERROR;
    }
    if (Tcl_GetIntFromObj(interp, objv[1], &status) != TCL_OK) {
        return TCL_ERROR;
    }
    return Result(interp,
                  Ns_ConnReturnNotice(conn, status,
                      Tcl_GetString(objv[2]), Tcl_GetString(objv[3])));
}


/*
 *----------------------------------------------------------------------
 *
 * NsTclReturnRedirectObjCmd --
 *
 *	Implements ns_returnredirect as obj command. 
 *
 * Results:
 *	Tcl result. 
 *
 * Side effects:
 *	See docs. 
 *
 *----------------------------------------------------------------------
 */

int
NsTclReturnRedirectObjCmd(ClientData arg, Tcl_Interp *interp, int objc, Tcl_Obj *CONST objv[])
{
    Ns_Conn *conn;

    if (objc != 2) {
        Tcl_WrongNumArgs(interp, 1, objv, "location");
        return TCL_ERROR;
    }
    if (GetConn(arg, interp, &conn) != TCL_OK) {
        return TCL_ERROR;
    }
    return Result(interp, Ns_ConnReturnRedirect(conn, Tcl_GetString(objv[1])));
}


/*
 *----------------------------------------------------------------------
 *
 * NsTclWriteObjCmd --
 *
 *	Implements ns_write as obj command.
 *
 * Results:
 *	Tcl result. 
 *
 * Side effects:
 *	See docs. 
 *
 *----------------------------------------------------------------------
 */

int
NsTclWriteObjCmd(ClientData arg, Tcl_Interp *interp, int objc, Tcl_Obj *CONST objv[])
{
    Ns_Conn *conn;
    char    *bytes;
    int      length;
    int      result;

    if (objc != 2) {
        Tcl_WrongNumArgs(interp, 1, objv, "string");
        return TCL_ERROR;
    }
    if (GetConn(arg, interp, &conn) != TCL_OK) {
        return TCL_ERROR;
    }

    /*
     * ns_write will treat data it is given as binary, until
     * it is specifically given permission to do otherwise through
     * the WriteEncodedFlag on the current conn.  This flag is
     * manipulated via ns_startcontent or ns_conn write_encoded
     */
    if (Ns_ConnGetWriteEncodedFlag(conn) &&
        (Ns_ConnGetEncoding(conn) != NULL)) {

        bytes = Tcl_GetStringFromObj(objv[1], &length);
        result = Ns_WriteCharConn(conn, bytes, length);

    } else {

        bytes = (char *) Tcl_GetByteArrayFromObj(objv[1], &length);
        result = Ns_WriteConn(conn, bytes, length);

    }

    return Result(interp, result);
}


/*
 *----------------------------------------------------------------------
 *
 * NsTclConnSendFpObjCmd --
 *
 *	Implements ns_connsendfp as obj command.
 *
 * Results:
 *	Tcl result. 
 *
 * Side effects:
 *	See docs. 
 *
 *----------------------------------------------------------------------
 */

int
NsTclConnSendFpObjCmd(ClientData arg, Tcl_Interp *interp, int objc, Tcl_Obj *CONST objv[])
{
    Ns_Conn     *conn;
    Tcl_Channel	 chan;
    int          len;

    if (objc != 3) {
        Tcl_WrongNumArgs(interp, 1, objv, "channel len");
        return TCL_ERROR;
    }
    if (GetConn(arg, interp, &conn) != TCL_OK) {
        return TCL_ERROR;
    }
    if (Ns_TclGetOpenChannel(interp, Tcl_GetString(objv[1]), 0, 1, &chan) 
			!= TCL_OK) {
        return TCL_ERROR;
    }
    if (Tcl_GetIntFromObj(interp, objv[2], &len) != TCL_OK) {
        return TCL_ERROR;
    }
    if (Ns_ConnSendChannel(conn, chan, len) != NS_OK) {
        Ns_TclPrintfResult(interp, "could not send %d bytes from channel %s",
                           len, Tcl_GetString(objv[1]));
        return TCL_ERROR;
    }
    return TCL_OK;
}

static int
Result(Tcl_Interp *interp, int result)
{
    Tcl_SetResult(interp, result == NS_OK ? "1" : "0", TCL_STATIC);
    return TCL_OK;
}


static int
GetConn(ClientData arg, Tcl_Interp *interp, Ns_Conn **connPtr)
{
    NsInterp *itPtr = arg;

    if (itPtr->conn == NULL) {
	Tcl_SetResult(interp, "no connection", TCL_STATIC);
	return TCL_ERROR;
    }
    *connPtr = itPtr->conn;
    return TCL_OK;
}
