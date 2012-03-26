/*
 * The contents of this file are subject to the MonetDB Public License
 * Version 1.1 (the "License"); you may not use this file except in
 * compliance with the License. You may obtain a copy of the License at
 * http://www.monetdb.org/Legal/MonetDBLicense
 *
 * Software distributed under the License is distributed on an "AS IS"
 * basis, WITHOUT WARRANTY OF ANY KIND, either express or implied. See the
 * License for the specific language governing rights and limitations
 * under the License.
 *
 * The Original Code is the MonetDB Database System.
 *
 * The Initial Developer of the Original Code is CWI.
 * Portions created by CWI are Copyright (C) 1997-July 2008 CWI.
 * Copyright August 2008-2012 MonetDB B.V.
 * All Rights Reserved.
 */

/*
 * This code was created by Peter Harvey (mostly during Christmas 98/99).
 * This code is LGPL. Please ensure that this message remains in future
 * distributions and uses of this code (thats about all I get out of it).
 * - Peter Harvey pharvey@codebydesign.com
 *
 * This file has been modified for the MonetDB project.  See the file
 * Copyright in this directory for more information.
 */

/**********************************************************************
 * SQLBindParameter()
 * CLI Compliance: ODBC (Microsoft)
 *
 * Note: this function is not supported (yet), it returns an error.
 * So parametrized SQL commands are not possible!
 * TODO: implement this function and corresponding behavior in
 * SQLPrepare() and SQLExecute().
 *
 * Author: Martin van Dinther, Sjoerd Mullender
 * Date  : 30 Aug 2002
 *
 **********************************************************************/

#include "ODBCGlobal.h"
#include "ODBCStmt.h"

SQLRETURN
SQLBindParameter_(ODBCStmt *stmt,
		  SQLUSMALLINT ParameterNumber,
		  SQLSMALLINT InputOutputType,
		  SQLSMALLINT ValueType,
		  SQLSMALLINT ParameterType,
		  SQLULEN ColumnSize,
		  SQLSMALLINT DecimalDigits,
		  SQLPOINTER ParameterValuePtr,
		  SQLLEN BufferLength,
		  SQLLEN *StrLen_or_IndPtr)
{
	ODBCDesc *apd, *ipd;
	ODBCDescRec *apdrec, *ipdrec;
	SQLRETURN rc;

	if (!isValidStmt(stmt))
		 return SQL_INVALID_HANDLE;

	clearStmtErrors(stmt);

	/* check input parameters */
	if (ParameterNumber <= 0) {
		/* Invalid descriptor index */
		addStmtError(stmt, "07009", NULL, 0);
		return SQL_ERROR;
	}
	/* For safety: limit the maximum number of columns to bind */
	if (ParameterNumber > MONETDB_MAX_BIND_COLS) {
		/* General error */
		addStmtError(stmt, "HY000", "Maximum number of bind columns (8192) exceeded", 0);
		return SQL_ERROR;
	}

	switch (InputOutputType) {
	case SQL_PARAM_INPUT:
		break;
	case SQL_PARAM_INPUT_OUTPUT:
	case SQL_PARAM_OUTPUT:
		/* Optional feature not implemented */
		addStmtError(stmt, "HYC00", "Output parameters are not supported", 0);
		return SQL_ERROR;
	default:
		/* Invalid parameter type */
		addStmtError(stmt, "HY105", NULL, 0);
		return SQL_ERROR;
	}

	if (ParameterValuePtr == NULL && StrLen_or_IndPtr == NULL
	    /* && InputOutputType != SQL_PARAM_OUTPUT */ ) {
		/* Invalid use of null pointer */
		addStmtError(stmt, "HY009", NULL, 0);
		return SQL_ERROR;
	}

	if (BufferLength < 0) {
		/* Invalid string or buffer length */
		addStmtError(stmt, "HY090", NULL, 0);
		return SQL_ERROR;
	}

	/* can't let SQLSetDescField below do this check since it
	   returns the wrong error code if the type is incorrect */
	switch (ValueType) {
	case SQL_C_CHAR:
	case SQL_C_WCHAR:
	case SQL_C_BINARY:
	case SQL_C_BIT:
	case SQL_C_STINYINT:
	case SQL_C_UTINYINT:
	case SQL_C_TINYINT:
	case SQL_C_SSHORT:
	case SQL_C_USHORT:
	case SQL_C_SHORT:
	case SQL_C_SLONG:
	case SQL_C_ULONG:
	case SQL_C_LONG:
	case SQL_C_SBIGINT:
	case SQL_C_UBIGINT:
	case SQL_C_NUMERIC:
	case SQL_C_FLOAT:
	case SQL_C_DOUBLE:
	case SQL_C_TYPE_DATE:
	case SQL_C_TYPE_TIME:
	case SQL_C_TYPE_TIMESTAMP:
	case SQL_C_INTERVAL_YEAR:
	case SQL_C_INTERVAL_MONTH:
	case SQL_C_INTERVAL_YEAR_TO_MONTH:
	case SQL_C_INTERVAL_DAY:
	case SQL_C_INTERVAL_HOUR:
	case SQL_C_INTERVAL_MINUTE:
	case SQL_C_INTERVAL_SECOND:
	case SQL_C_INTERVAL_DAY_TO_HOUR:
	case SQL_C_INTERVAL_DAY_TO_MINUTE:
	case SQL_C_INTERVAL_DAY_TO_SECOND:
	case SQL_C_INTERVAL_HOUR_TO_MINUTE:
	case SQL_C_INTERVAL_HOUR_TO_SECOND:
	case SQL_C_INTERVAL_MINUTE_TO_SECOND:
	case SQL_C_GUID:
	case SQL_C_DEFAULT:
		break;
	default:
		/* Invalid application buffer type */
		addStmtError(stmt, "HY003", NULL, 0);
		return SQL_ERROR;
	}

	apd = stmt->ApplParamDescr;
	ipd = stmt->ImplParamDescr;

	apdrec = addODBCDescRec(apd, ParameterNumber);
	ipdrec = addODBCDescRec(ipd, ParameterNumber);

	/* we disallow types not supported by the server */
	switch (ParameterType) {
	case SQL_CHAR:
	case SQL_VARCHAR:
	case SQL_LONGVARCHAR:
/* 	case SQL_BINARY: */
	case SQL_VARBINARY:
	case SQL_LONGVARBINARY:
	case SQL_TYPE_DATE:
	case SQL_INTERVAL_MONTH:
/* 	case SQL_INTERVAL_YEAR: */
/* 	case SQL_INTERVAL_YEAR_TO_MONTH: */
/* 	case SQL_INTERVAL_DAY: */
/* 	case SQL_INTERVAL_HOUR: */
/* 	case SQL_INTERVAL_MINUTE: */
/* 	case SQL_INTERVAL_DAY_TO_HOUR: */
/* 	case SQL_INTERVAL_DAY_TO_MINUTE: */
/* 	case SQL_INTERVAL_HOUR_TO_MINUTE: */
		ipdrec->sql_desc_length = ColumnSize;
		break;
	case SQL_TYPE_TIME:
	case SQL_TYPE_TIMESTAMP:
	case SQL_INTERVAL_SECOND:
/* 	case SQL_INTERVAL_DAY_TO_SECOND: */
/* 	case SQL_INTERVAL_HOUR_TO_SECOND: */
/* 	case SQL_INTERVAL_MINUTE_TO_SECOND: */
		ipdrec->sql_desc_precision = DecimalDigits;
		ipdrec->sql_desc_length = ColumnSize;
		break;
	case SQL_DECIMAL:
/* 	case SQL_NUMERIC: */
		ipdrec->sql_desc_precision = (SQLSMALLINT) ColumnSize;
		ipdrec->sql_desc_scale = DecimalDigits;
		break;
/* 	case SQL_FLOAT: */
	case SQL_REAL:
	case SQL_DOUBLE:
		ipdrec->sql_desc_precision = (SQLSMALLINT) ColumnSize;
		break;
/* 	case SQL_WCHAR: */
/* 	case SQL_WVARCHAR: */
/* 	case SQL_WLONGVARCHAR: */
	case SQL_BIT:
	case SQL_TINYINT:
	case SQL_SMALLINT:
	case SQL_INTEGER:
	case SQL_BIGINT:
/* 	case SQL_GUID: */
		break;
	default:
		/* Invalid SQL data type */
		addStmtError(stmt, "HY004", NULL, 0);
		return SQL_ERROR;

	/* these types are not allowed by the server */
	case SQL_BINARY:
	case SQL_INTERVAL_YEAR:
	case SQL_INTERVAL_YEAR_TO_MONTH:
	case SQL_INTERVAL_DAY:
	case SQL_INTERVAL_HOUR:
	case SQL_INTERVAL_MINUTE:
	case SQL_INTERVAL_DAY_TO_HOUR:
	case SQL_INTERVAL_DAY_TO_MINUTE:
	case SQL_INTERVAL_HOUR_TO_MINUTE:
	case SQL_INTERVAL_DAY_TO_SECOND:
	case SQL_INTERVAL_HOUR_TO_SECOND:
	case SQL_INTERVAL_MINUTE_TO_SECOND:
	case SQL_NUMERIC:
	case SQL_FLOAT:
	case SQL_WCHAR:
	case SQL_WVARCHAR:
	case SQL_WLONGVARCHAR:
	case SQL_GUID:
		/* Optional feature not implemented */
		addStmtError(stmt, "HYC00", NULL, 0);
		return SQL_ERROR;
	}

	rc = SQLSetDescField_(apd, ParameterNumber, SQL_DESC_CONCISE_TYPE, (SQLPOINTER) (ssize_t) ValueType, 0);
	if (!SQL_SUCCEEDED(rc))
		return rc;
	rc = SQLSetDescField_(ipd, ParameterNumber, SQL_DESC_CONCISE_TYPE, (SQLPOINTER) (ssize_t) ParameterType, 0);
	if (!SQL_SUCCEEDED(rc))
		return rc;
	ipdrec->sql_desc_parameter_type = InputOutputType;
	apdrec->sql_desc_data_ptr = ParameterValuePtr;
	apdrec->sql_desc_octet_length = BufferLength;
	apdrec->sql_desc_indicator_ptr = StrLen_or_IndPtr;
	apdrec->sql_desc_octet_length_ptr = StrLen_or_IndPtr;

	return SQL_SUCCESS;
}

SQLRETURN SQL_API
SQLBindParameter(SQLHSTMT StatementHandle,
		 SQLUSMALLINT ParameterNumber,
		 SQLSMALLINT InputOutputType,
		 SQLSMALLINT ValueType,
		 SQLSMALLINT ParameterType,
		 SQLULEN ColumnSize,
		 SQLSMALLINT DecimalDigits,
		 SQLPOINTER ParameterValuePtr,
		 SQLLEN BufferLength,
		 SQLLEN *StrLen_or_IndPtr)
{
#ifdef ODBCDEBUG
	ODBCLOG("SQLBindParameter " PTRFMT " %u %d %s %s " ULENFMT " %d\n",
		PTRFMTCAST StatementHandle, (unsigned int) ParameterNumber,
		(int) InputOutputType, translateCType(ValueType),
		translateSQLType(ParameterType),
		ULENCAST ColumnSize, (int) DecimalDigits);
#endif

	return SQLBindParameter_((ODBCStmt *) StatementHandle, ParameterNumber,
				 InputOutputType, ValueType, ParameterType,
				 ColumnSize, DecimalDigits, ParameterValuePtr,
				 BufferLength, StrLen_or_IndPtr);
}
