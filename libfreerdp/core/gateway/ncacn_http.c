/**
 * FreeRDP: A Remote Desktop Protocol Implementation
 * RPC over HTTP (ncacn_http)
 *
 * Copyright 2012 Marc-Andre Moreau <marcandre.moreau@gmail.com>
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include "ncacn_http.h"

#include <winpr/crt.h>
#include <winpr/tchar.h>
#include <winpr/dsparse.h>

#include <openssl/rand.h>

STREAM* rpc_ntlm_http_request(rdpRpc* rpc, SecBuffer* ntlm_token, int content_length, TSG_CHANNEL channel)
{
	STREAM* s;
	char* base64_ntlm_token;
	HttpContext* http_context;
	HttpRequest* http_request;

	http_request = http_request_new();
	base64_ntlm_token = crypto_base64_encode(ntlm_token->pvBuffer, ntlm_token->cbBuffer);

	if (channel == TSG_CHANNEL_IN)
	{
		http_context = rpc->NtlmHttpIn->context;
		http_request_set_method(http_request, "RPC_IN_DATA");
	}
	else if (channel == TSG_CHANNEL_OUT)
	{
		http_context = rpc->NtlmHttpOut->context;
		http_request_set_method(http_request, "RPC_OUT_DATA");
	}
	else
	{
		return NULL;
	}

	http_request->ContentLength = content_length;
	http_request_set_uri(http_request, http_context->URI);

	http_request_set_auth_scheme(http_request, "NTLM");
	http_request_set_auth_param(http_request, base64_ntlm_token);

	s = http_request_write(http_context, http_request);
	http_request_free(http_request);

	free(base64_ntlm_token);

	return s;
}

int rpc_ncacn_http_send_in_channel_request(rdpRpc* rpc)
{
	STREAM* s;
	int content_length;
	BOOL continue_needed;
	rdpNtlm* ntlm = rpc->NtlmHttpIn->ntlm;

	continue_needed = ntlm_authenticate(ntlm);

	content_length = (continue_needed) ? 0 : 0x40000000;

	s = rpc_ntlm_http_request(rpc, &ntlm->outputBuffer, content_length, TSG_CHANNEL_IN);

	DEBUG_RPC("\n%s", s->data);
	rpc_in_write(rpc, s->data, s->size);
	stream_free(s);

	return 0;
}

int rpc_ncacn_http_recv_in_channel_response(rdpRpc* rpc)
{
	int ntlm_token_length;
	BYTE* ntlm_token_data;
	HttpResponse* http_response;
	rdpNtlm* ntlm = rpc->NtlmHttpIn->ntlm;

	http_response = http_response_recv(rpc->TlsIn);

	ntlm_token_data = NULL;
	crypto_base64_decode((BYTE*) http_response->AuthParam, strlen(http_response->AuthParam),
			&ntlm_token_data, &ntlm_token_length);

	ntlm->inputBuffer.pvBuffer = ntlm_token_data;
	ntlm->inputBuffer.cbBuffer = ntlm_token_length;

	http_response_free(http_response);

	return 0;
}

int rpc_ncacn_http_ntlm_init(rdpRpc* rpc, TSG_CHANNEL channel)
{
	rdpNtlm* ntlm = NULL;
	rdpSettings* settings;

	settings = rpc->settings;

	if (channel == TSG_CHANNEL_IN)
		ntlm = rpc->NtlmHttpIn->ntlm;
	else if (channel == TSG_CHANNEL_OUT)
		ntlm = rpc->NtlmHttpOut->ntlm;

	if (settings->GatewayUseSameCredentials)
	{
		ntlm_client_init(ntlm, TRUE, settings->Username,
			settings->Domain, settings->Password);
	}
	else
	{
		ntlm_client_init(ntlm, TRUE, settings->GatewayUsername,
			settings->GatewayDomain, settings->GatewayPassword);
	}

	ntlm_client_make_spn(ntlm, NULL, settings->GatewayHostname);
	//ntlm_client_make_spn(ntlm, _T("HTTP"), settings->GatewayHostname);

	return 0;
}

BOOL rpc_ntlm_http_in_connect(rdpRpc* rpc)
{
	rdpNtlm* ntlm = rpc->NtlmHttpIn->ntlm;

	rpc_ncacn_http_ntlm_init(rpc, TSG_CHANNEL_IN);

	/* Send IN Channel Request */

	rpc_ncacn_http_send_in_channel_request(rpc);

	/* Receive IN Channel Response */

	rpc_ncacn_http_recv_in_channel_response(rpc);

	/* Send IN Channel Request */

	rpc_ncacn_http_send_in_channel_request(rpc);

	ntlm_client_uninit(ntlm);
	ntlm_free(ntlm);

	return TRUE;
}

int rpc_ncacn_http_send_out_channel_request(rdpRpc* rpc)
{
	STREAM* s;
	int content_length;
	BOOL continue_needed;
	rdpNtlm* ntlm = rpc->NtlmHttpOut->ntlm;

	continue_needed = ntlm_authenticate(ntlm);

	content_length = (continue_needed) ? 0 : 76;

	s = rpc_ntlm_http_request(rpc, &ntlm->outputBuffer, content_length, TSG_CHANNEL_OUT);

	DEBUG_RPC("\n%s", s->data);
	rpc_out_write(rpc, s->data, s->size);
	stream_free(s);

	return 0;
}

int rpc_ncacn_http_recv_out_channel_response(rdpRpc* rpc)
{
	int ntlm_token_length;
	BYTE* ntlm_token_data;
	HttpResponse* http_response;
	rdpNtlm* ntlm = rpc->NtlmHttpOut->ntlm;

	http_response = http_response_recv(rpc->TlsOut);

	ntlm_token_data = NULL;
	crypto_base64_decode((BYTE*) http_response->AuthParam, strlen(http_response->AuthParam),
			&ntlm_token_data, &ntlm_token_length);

	ntlm->inputBuffer.pvBuffer = ntlm_token_data;
	ntlm->inputBuffer.cbBuffer = ntlm_token_length;

	http_response_free(http_response);

	return 0;
}

BOOL rpc_ntlm_http_out_connect(rdpRpc* rpc)
{
	rdpNtlm* ntlm = rpc->NtlmHttpOut->ntlm;

	rpc_ncacn_http_ntlm_init(rpc, TSG_CHANNEL_OUT);

	/* Send OUT Channel Request */

	rpc_ncacn_http_send_out_channel_request(rpc);

	/* Receive OUT Channel Response */

	rpc_ncacn_http_recv_out_channel_response(rpc);

	/* Send OUT Channel Request */

	rpc_ncacn_http_send_out_channel_request(rpc);

	ntlm_client_uninit(ntlm);
	ntlm_free(ntlm);

	return TRUE;
}

