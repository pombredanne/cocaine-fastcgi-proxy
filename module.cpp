/*
	Copyright (c) 2011-2012 Andrey Sibiryov <me@kobology.ru>
	Copyright (c) 2011-2012 Other contributors as noted in the AUTHORS file.

	This file is part of Cocaine.

	Cocaine is free software; you can redistribute it and/or modify
	it under the terms of the GNU Lesser General Public License as published by
	the Free Software Foundation; either version 3 of the License, or
	(at your option) any later version.

	Cocaine is distributed in the hope that it will be useful,
	but WITHOUT ANY WARRANTY; without even the implied warranty of
	MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
	GNU Lesser General Public License for more details.

	You should have received a copy of the GNU Lesser General Public License
	along with this program. If not, see <http://www.gnu.org/licenses/>. 
*/

#include <boost/tokenizer.hpp>
#include <boost/foreach.hpp>
#include <boost/regex.hpp>
#include <boost/format.hpp>

#include <fastcgi2/config.h>
#include <fastcgi2/component_factory.h>
#include <fastcgi2/cookie.h>
#include <fastcgi2/except.h>
#include <fastcgi2/util.h>

#include <cocaine/dealer/response.hpp>
#include <cocaine/dealer/utils/error.hpp>

#include "module.hpp"

using namespace cocaine::dealer;

fastcgi_module_t::fastcgi_module_t(fastcgi::ComponentContext* context) :
	fastcgi::Component(context),
	m_logger(NULL),
	m_dealer(NULL)
{}

fastcgi_module_t::~fastcgi_module_t() {
	m_dealer.reset();
}

struct cocaine_response_t {
	int code;

	typedef std::vector<
		std::pair<
			std::string,
			std::string
		>
	> header_vector_t;

	header_vector_t headers;
};

namespace msgpack {
	inline cocaine_response_t& operator >> (object o, cocaine_response_t& v) {
		if (o.type != type::MAP) { 
			throw type_error();
		}

		msgpack::object_kv* p = o.via.map.ptr;
		msgpack::object_kv* const pend = o.via.map.ptr + o.via.map.size;

		for (; p < pend; ++p) {
			std::string key;

			p->key.convert(&key);

			if (!key.compare("code")) {
				p->val.convert(&(v.code));
			}
			else if (!key.compare("headers")) {
				p->val.convert(&(v.headers));
			}
		}

		return v;
	}

	template<class Stream>
	inline packer<Stream>& operator << (packer<Stream>& packer, const fastcgi::Request& request) {
		typedef std::vector<
			std::string
		> string_vector_t;

		string_vector_t header_names,
						cookie_names,
						argument_names,
						argument_values;

		packer.pack_map(3);

		// Metadata

		packer.pack(std::string("meta"));

		packer.pack_map(11);

		packer.pack(std::string("secure"));
		packer.pack(request.isSecure());

		packer.pack(std::string("url"));
		packer.pack(request.getUrl());

		packer.pack(std::string("host"));
		packer.pack(request.getHost());

		packer.pack(std::string("method"));
		packer.pack(request.getRequestMethod());

		packer.pack(std::string("query_string"));
		packer.pack(request.getQueryString());

		packer.pack(std::string("remote_addr"));
		packer.pack(request.getRemoteAddr());

		packer.pack(std::string("server_addr"));
		packer.pack(request.getServerAddr());

		packer.pack(std::string("path_info"));
		packer.pack(request.getPathInfo());

		packer.pack(std::string("script_name"));
		packer.pack(request.getScriptName());

		packer.pack(std::string("headers"));
		packer.pack_map(request.countHeaders());

		request.headerNames(header_names);

		string_vector_t::const_iterator it;
		
		it = header_names.begin();

		for (; it != header_names.end(); ++it) {
			packer.pack(*it);
			packer.pack(request.getHeader(*it));
		}

		packer.pack(std::string("cookies"));
		packer.pack_map(request.countCookie());

		request.cookieNames(cookie_names);

		it = cookie_names.begin();

		for (; it != cookie_names.end(); ++it) {
			packer.pack(*it);
			packer.pack(request.getCookie(*it));
		}

		// Query arguments

		packer.pack(std::string("request"));

		request.argNames(argument_names);

		packer.pack_map(argument_names.size());

		it = argument_names.begin();

		for (; it != argument_names.end(); ++it) {
			request.getArg(*it, argument_values);

			packer.pack(*it);

			if(argument_values.size() == 1) {
				packer.pack(argument_values[0]);
			}
			else {
				packer.pack(argument_values);
			}
		}

		// Body

		std::string body;

		request.requestBody().toString(body);

		packer.pack(std::string("body"));
		packer.pack(body);

		return packer;
	}
}

bool 
fastcgi_module_t::get_config_param(bool& param, const std::string& path) {
	std::string component_path(context()->getComponentXPath());

	try {
		const fastcgi::Config* config = context()->getConfig();
		std::string param_value(config->asString(component_path + path));
		boost::trim(param_value);
		boost::to_lower(param_value);

		if (param_value == "1" || param_value == "true") {
			param = true;
		}
		else {
			param = false;
		}
	}
	catch (const std::exception& ex) {
		if (m_logger) {
			std::string log_str = "can't get %s config value, details: %s";
			log()->debug(log_str.c_str(), path.c_str(), ex.what());
		}

		return false;
	}

	return true;
}

bool
fastcgi_module_t::header_value(bool& value,
							   const std::string& header_name,
							   fastcgi::Request& request)
{
	std::string header_value = request.getHeader(header_name);
	if (header_value.empty()) {
		return false;
	}

	boost::trim(header_value);
	boost::to_lower(header_value);

	if (header_value == "1" || header_value == "true") {
		value = true;
	}
	else {
		value = false;
	}

	return true;
}

void
fastcgi_module_t::update_policy_from_headers(message_policy_t& policy,
											 fastcgi::Request& request)
{
	header_value(policy.urgent, "dealer_policy_urgent", request);
	header_value(policy.persistent, "dealer_policy_persistent", request);
	header_value(policy.timeout, "dealer_policy_timeout", request);
	//header_value(policy.ack_timeout, "dealer_policy_ack_timeout", request);
	header_value(policy.deadline, "dealer_policy_deadline", request);
	header_value(policy.max_retries, "dealer_policy_max_retries", request);
}

void
fastcgi_module_t::update_policy_from_config(message_policy_t& policy)
{
	std::set<std::string, bool>::iterator it = m_available_policy_params.begin();
	for (; it != m_available_policy_params.end(); ++it) {
		if (*it == "urgent") {
			policy.urgent = m_config_policy.urgent;
		}
		else if (*it == "persistent") {
			policy.persistent = m_config_policy.persistent;
		}
		else if (*it == "timeout") {
			policy.timeout = m_config_policy.timeout;
		}
		else if (*it == "ack_timeout") {
			//policy.ack_timeout = m_config_policy.ack_timeout;
		}
		else if (*it == "deadline") {
			policy.deadline = m_config_policy.deadline;
		}
		else if (*it == "max_retries") {
			policy.max_retries = m_config_policy.max_retries;
		}
	}
}

void
fastcgi_module_t::handleRequest(fastcgi::Request* request,
								fastcgi::HandlerContext* context)
{
	(void)context;

	std::string name(request->getScriptFilename());

	if (name.compare(0, sizeof("/ping") - 1, "/ping") == 0) {
		request->setStatus(200);
		request->setContentType("text/plain");
		request->write("ok", sizeof("ok") - 1);
		return;
	}
	
	std::string path_str;
	boost::optional<std::string> path_val = get_path_from_mapping(name);
	path_str = path_val ? *path_val : name;

	boost::shared_ptr<response_t> future; 
	message_path_t path(make_path(path_str));
	
	try {
		message_policy_t policy = m_dealer->policy_for_service(path.service_alias);
		update_policy_from_config(policy);
		update_policy_from_headers(policy, *request);

		future = m_dealer->send_message(*request, path, policy);
	}
	catch (const dealer_error& e) {
		log()->error(
			"unable to send message to '%s/%s' - %s",
			path.service_alias.c_str(),
			path.handle_name.c_str(),
			e.what()
		);

		throw fastcgi::HttpException(e.code());
	}
	catch (const internal_error& e) {
		log()->error(
			"unable to send message to '%s/%s' - %s",
			path.service_alias.c_str(),
			path.handle_name.c_str(),
			e.what()
		);
		
		throw fastcgi::HttpException(400);
	}

	request->setStatus(200);

	if (request->getRequestMethod() == "HEAD") {
		return;
	}

	try {
		data_container chunk;

		// Get the first chunk of data - it is meta
		future->get(&chunk);

		try {
			msgpack::unpacked unpacked;
			cocaine_response_t response;

			msgpack::unpack(
				&unpacked,
				static_cast<const char*>(chunk.data()),
				chunk.size()
			);

			unpacked.get().convert(&response);

			request->setStatus(response.code);

			cocaine_response_t::header_vector_t::iterator it = response.headers.begin();
			for (; it != response.headers.end(); it++) {
				request->setHeader(it->first, it->second);
			}
		}
		catch (std::exception &e) {
			log()->error(
				"unable to process response for '%s/%s' - %s",
				path.service_alias.c_str(),
				path.handle_name.c_str(),
				e.what()
			);

			throw fastcgi::HttpException(503);
		}

		while (future->get(&chunk)) {
			request->write(
				static_cast<const char*>(chunk.data()),
				chunk.size()
			);
		}
	}
	catch(const dealer_error& e) {
		std::string error_message = "unable to process message for '";
		error_message += path.service_alias + "/";
		error_message += path.handle_name + "' - ";
		error_message += e.what();

		log()->error(error_message.c_str());

		int http_error_code = 500;

		switch(e.code()) {
			case 1: // invocation_error
				http_error_code = 500;
				break;
			case 2: // resource_error
				http_error_code = 503; // 503 Service Unavailable
				break;
			case 3: // timeout_error
			case 4: // deadline_error
				http_error_code = 504; // 504 Gateway Timeout
				break;
		}
		
		request->write(
			static_cast<const char*>(error_message.data()),
			error_message.size()
		);

		throw fastcgi::HttpException(http_error_code);
	}
	catch(const internal_error& e) {
		log()->error(
			"unable to process message for '%s/%s' - %s",
			path.service_alias.c_str(),
			path.handle_name.c_str(),
			e.what()
		);

		throw fastcgi::HttpException(400);
	}
	catch(const fastcgi::HttpException &e) {
		throw;
	}
}

message_path_t
fastcgi_module_t::make_path(const std::string& script_name) const {
	typedef boost::tokenizer<
		boost::char_separator<char>
	> tokenizer_type;

	boost::char_separator<char> separator("/");
	tokenizer_type tokenizer(script_name, separator);
	
	std::vector<std::string> tokens;

	std::copy(
		tokenizer.begin(),
		tokenizer.end(),
		std::back_inserter(tokens)
	);

	if (tokens.size() != 2) {
		log()->error(
			"invalid message path, got %d path components",
			tokens.size()
		);

		throw fastcgi::HttpException(400);
	}

	return message_path_t(tokens[0], tokens[1]);
}

void
fastcgi_module_t::onLoad() {
	assert(NULL == m_logger);

	const fastcgi::Config * config = context()->getConfig();
	std::string path(context()->getComponentXPath());

	m_logger = context()->findComponent<fastcgi::Logger>(config->asString(path + "/logger"));

	if (!m_logger) {
		throw std::logic_error("can't find logger");
	}

	std::string config_path;
	m_available_policy_params.clear();

	if (get_config_param(m_config_policy.urgent, "/client/message_policy/urgent")) {
		m_available_policy_params.insert("urgent");
	}

	if (get_config_param(m_config_policy.persistent, "/client/message_policy/persistent")) {
		m_available_policy_params.insert("persistent");
	}

	if (get_config_param(m_config_policy.timeout, "/client/message_policy/timeout")) {
		m_available_policy_params.insert("timeout");
	}

	//if (get_config_param(m_config_policy.ack_timeout, "/client/message_policy/ack_timeout")) {
		//m_available_policy_params.insert("ack_timeout");
	//}

	if (get_config_param(m_config_policy.deadline, "/client/message_policy/deadline")) {
		m_available_policy_params.insert("deadline");
	}

	if (get_config_param(m_config_policy.max_retries, "/client/message_policy/max_retries")) {
		m_available_policy_params.insert("max_retries");
	}

	get_config_param(config_path, "/client/configuration");

	std::vector<std::string> mappings;
	std::string mapping_path = path + "/mapping/path";
	
	config->subKeys(mapping_path, mappings);
	BOOST_FOREACH(std::string& mapping_str, mappings) {
		Mapping mapping;
		
		mapping.pattern = config->asString(mapping_str + "/@pattern");
		mapping.app = config->asString(mapping_str + "/@app");
		mapping.handle = config->asString(mapping_str + "/@handle");
		
		url_mappings_.push_back(mapping);
	}
	
	m_dealer.reset(new dealer_t(config_path));
}

void
fastcgi_module_t::onUnload() {
	m_dealer.reset();
}

boost::optional<std::string> 
fastcgi_module_t::get_path_from_mapping(const std::string& path) const {
	BOOST_FOREACH(const Mapping& mapping, url_mappings_) {
		bool result = boost::regex_match(path, mapping.pattern);
		if (result) {
			return boost::str(boost::format("%1%/%2%") % mapping.app % mapping.handle );
		}
	}
	return false;
}

FCGIDAEMON_REGISTER_FACTORIES_BEGIN()
FCGIDAEMON_ADD_DEFAULT_FACTORY("cocaine-fastcgi", fastcgi_module_t);
FCGIDAEMON_REGISTER_FACTORIES_END()
