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
 * adprequest.c --
 *
 *	ADP connection request support.
 */

static const char *RCSID = "@(#) $Header$, compiled: " __DATE__ " " __TIME__;

#include "nsd.h"

static int AdpFlush(NsInterp *itPtr, int stream);


/*
 *----------------------------------------------------------------------
 *
 * NsAdpProc --
 *
 *	Check for a normal file and call Ns_AdpRequest.
 *
 * Results:
 *	A standard AOLserver request result.
 *
 * Side effects:
 *	Depends on code embedded within page.
 *
 *----------------------------------------------------------------------
 */

int
NsAdpProc(void *arg, Ns_Conn *conn)
{
    Ns_DString file;
    int status;

    Ns_DStringInit(&file);
    Ns_UrlToFile(&file, Ns_ConnServer(conn), conn->request->url);
    status = Ns_AdpRequest(conn, file.string);
    Ns_DStringFree(&file);
    return status;
}


/*
 *----------------------------------------------------------------------
 *
 * Ns_AdpRequest -
 *
 *  	Invoke a file for an ADP request.
 *
 * Results:
 *	A standard AOLserver request result.
 *
 * Side effects:
 *	Depends on code embedded within page.
 *
 *----------------------------------------------------------------------
 */

int
Ns_AdpRequest(Ns_Conn *conn, char *file)
{
    Conn	     *connPtr = (Conn *) conn;
    Tcl_Interp       *interp;
    Tcl_DString	      rds, tds;
    NsInterp          *itPtr;
    int               status;
    char             *type, *start;
    Ns_Set           *setPtr;
    NsServer	     *servPtr;
    Tcl_Obj	     *objv[2];
    
    /*
     * Verify the file exists.
     */

    if (access(file, R_OK) != 0) {
	return Ns_ConnReturnNotFound(conn);
    }

    /*
     * Get the current connection's interp.
     */

    interp = Ns_GetConnInterp(conn);
    itPtr = NsGetInterp(interp);
    servPtr = itPtr->servPtr;

    /*
     * Set the response type and output buffers.
     */

    Tcl_DStringInit(&rds);
    Tcl_DStringInit(&tds);
    itPtr->adp.responsePtr = &rds;
    itPtr->adp.typePtr = &tds;

    /*
     * Determine the output type.  This will set both the input
     * and output encodings by default.
     */

    type = Ns_GetMimeType(file);
    if (type == NULL || (strcmp(type, "*/*") == 0)) {
        type = NSD_TEXTHTML;
    }
    NsAdpSetMimeType(itPtr, type);

    /*
     * Set the old conn variable for backwards compatibility.
     */

    Tcl_SetVar2(interp, "conn", NULL, connPtr->idstr, TCL_GLOBAL_ONLY);
    Tcl_ResetResult(interp);

    /*
     * Enable TclPro debugging if requested.
     */

    if (servPtr->adp.enabledebug &&
	STREQ(conn->request->method, "GET") &&
	(setPtr = Ns_ConnGetQuery(conn)) != NULL) {
	itPtr->adp.debugFile = Ns_SetIGet(setPtr, "debug");
    }

    /*
     * Include the ADP with the special start page and null args.
     */

    start = servPtr->adp.startpage ? servPtr->adp.startpage : file;
    objv[0] = Tcl_NewStringObj(start, -1);
    objv[1] = Tcl_NewStringObj(file, -1);
    Tcl_IncrRefCount(objv[0]);
    Tcl_IncrRefCount(objv[1]);
    if (NsAdpInclude(itPtr, start, 2, objv) != TCL_OK &&
        itPtr->adp.exception != ADP_RETURN &&
        itPtr->adp.exception != ADP_ABORT) {
	Ns_TclLogError(interp);
    }
    Tcl_DecrRefCount(objv[0]);
    Tcl_DecrRefCount(objv[1]);

    /*
     * If a response was not generated by the ADP code,
     * generate one now.
     */

    status = NS_OK;
    if (!(conn->flags & NS_CONN_SENTHDRS)
	    && itPtr->adp.exception != ADP_ABORT) {
	status = AdpFlush(itPtr, 0);
    }

    /*
     * Cleanup the per-thead ADP context.
     */

    itPtr->adp.outputPtr = NULL;
    itPtr->adp.responsePtr = NULL;
    itPtr->adp.typePtr = NULL;
    itPtr->adp.exception = ADP_OK;
    itPtr->adp.stream = 0;
    itPtr->adp.debugLevel = 0;
    itPtr->adp.debugInit = 0;
    itPtr->adp.debugFile = NULL;
    Tcl_DStringFree(&rds);
    Tcl_DStringFree(&tds);
    return status;
}


/*
 *----------------------------------------------------------------------
 *
 * NsAdpFlush --
 *
 *	Flush current response output to connection.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	None unless streaming is enabled in which case AdpFlush
 *	is called.
 *
 *----------------------------------------------------------------------
 */

void
NsAdpFlush(NsInterp *itPtr)
{
    if (itPtr->adp.stream
	&& itPtr->adp.responsePtr != NULL
	&& itPtr->adp.responsePtr->length > 0) {
	if (AdpFlush(itPtr, 1) != NS_OK) {
	    itPtr->adp.stream = 0;
	}
    }
}


/*
 *----------------------------------------------------------------------
 *
 * NsAdpStream --
 *
 *	Turn streaming mode on.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	Headers and current data, if any, are flushed.
 *
 *----------------------------------------------------------------------
 */

void
NsAdpStream(NsInterp *itPtr)
{
    if (!itPtr->adp.stream && itPtr->conn != NULL) {
    	itPtr->adp.stream = 1;
	NsAdpFlush(itPtr);
    }
}


/*
 *----------------------------------------------------------------------
 *
 * NsAdpSetMimeType --
 *
 *	Sets the mime type and connection encoding for this adp.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *  	Type may effect output encoding charset.
 *
 *----------------------------------------------------------------------
 */

void
NsAdpSetMimeType(NsInterp *itPtr, char *type)
{
    Tcl_Encoding encoding;

    if (itPtr->adp.typePtr != NULL) {
	Tcl_DStringFree(itPtr->adp.typePtr);
	Tcl_DStringAppend(itPtr->adp.typePtr, type, -1);
	encoding = Ns_GetTypeEncoding(type);
	if (encoding != NULL) {
	    Ns_ConnSetEncoding(itPtr->conn, encoding);
            Ns_ConnSetUrlEncoding(itPtr->conn, encoding);
	}
    }
}


/*
 *----------------------------------------------------------------------
 *
 * NsFreeAdp --
 *
 *	Interp delete callback to free ADP resources.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *  	None.
 *
 *----------------------------------------------------------------------
 */

void
NsFreeAdp(NsInterp *itPtr)
{
    if (itPtr->adp.cache != NULL) {
	Ns_CacheDestroy(itPtr->adp.cache);
    }
}


/*
 *----------------------------------------------------------------------
 *
 * AdpFlush --
 *
 *	Flush the headers and/or ADP content.
 *
 * Results:
 *	NS_OK or NS_ERROR if a connection write routine failed.
 *
 * Side effects:
 *  	Content is encoded and/or sent.
 *
 *----------------------------------------------------------------------
 */

static int
AdpFlush(NsInterp *itPtr, int stream)
{
    Tcl_Encoding encoding;
    Ns_Conn *conn;
    Tcl_DString  ds;
    int result, len;
    char *buf, *type;

    Tcl_DStringInit(&ds);
    conn = itPtr->conn;
    buf = itPtr->adp.responsePtr->string;
    len = itPtr->adp.responsePtr->length;
    type = itPtr->adp.typePtr->string;

    /*
     * If necessary, encode the output.
     */

    encoding = Ns_ConnGetEncoding(conn);
    if (encoding != NULL) {
	Tcl_UtfToExternalDString(encoding, buf, len, &ds);
	buf = ds.string;
	len = ds.length;
    }

    /*
     * Flush out the headers now that the encoded output length
     * is known for non-streaming output.
     */

    if (!(conn->flags & NS_CONN_SENTHDRS)) {
	if (itPtr->servPtr->adp.enableexpire) {
	    Ns_ConnCondSetHeaders(conn, "Expires", "now");
	}
	Ns_ConnSetRequiredHeaders(conn, type, stream ? 0 : len);
	Ns_ConnQueueHeaders(conn, 200);
    }

    /*
     * Write the output buffer and if not streaming, close the
     * connection.
     */

    if (conn->flags & NS_CONN_SKIPBODY) {
        buf = NULL;
        len = 0;
    }

    result = Ns_WriteConn(conn, buf, len);
    if (result == NS_OK && !stream) {
	result = Ns_ConnClose(conn);
    }

    Tcl_DStringFree(&ds);
    Tcl_DStringTrunc(itPtr->adp.responsePtr, 0);
    return result;
}
