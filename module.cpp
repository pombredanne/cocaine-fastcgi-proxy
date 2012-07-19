//
// Copyright (C) 2011-2012 Andrey Sibiryov <me@kobology.ru>
//
// Licensed under the BSD 2-Clause License (the "License");
// you may not use this file except in compliance with the License.
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.
//

#include <boost/tokenizer.hpp>

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
                        argument_values,
                        file_names;

        packer.pack_map(2);

        // Metadata
        // --------

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
        // ---------------

        packer.pack(std::string("request"));

        request.remoteFiles(file_names);

        packer.pack_map(request.countArgs() + file_names.size());

        request.argNames(argument_names);

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

        // Uploads
        // -------

        std::string contents;

        it = file_names.begin();
        for(; it != file_names.end(); ++it) {
            packer.pack(*it);

            packer.pack_map(3);

            packer.pack(std::string("name"));
            packer.pack(request.remoteFileName(*it));

            packer.pack(std::string("type"));
            packer.pack(request.remoteFileType(*it));

            request.remoteFile(*it).toString(contents); 

            packer.pack(std::string("contents"));
            packer.pack(contents);
        }

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
    header_value(policy.timeout, "dealer_policy_timeout", request);
    header_value(policy.deadline, "dealer_policy_deadline", request);
    header_value(policy.max_retries, "dealer_policy_max_retries", request);
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

    boost::shared_ptr<response_t> future;
    message_path_t path(make_path(name));

    message_policy_t mp = m_default_policy;
    update_policy_from_headers(mp, *request);

    try {
        future = m_dealer->send_message(*request, path, mp);
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
        log()->error(
            "unable to process message for '%s/%s' - %s",
            path.service_alias.c_str(),
            path.handle_name.c_str(),
            e.what()
        );
        
        throw fastcgi::HttpException(e.code());
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

    m_default_policy.max_retries = -1;
    m_default_policy.deadline = 1.0;

    get_config_param(m_default_policy.urgent, "/client/message_policy/urgent");
    get_config_param(m_default_policy.timeout, "/client/message_policy/timeout");
    get_config_param(m_default_policy.deadline, "/client/message_policy/deadline");
    get_config_param(m_default_policy.max_retries, "/client/message_policy/max_retries");
    get_config_param(config_path, "/client/configuration");

    m_dealer.reset(new dealer_t(config_path));
}

void
fastcgi_module_t::onUnload() {
    m_dealer.reset();
}

FCGIDAEMON_REGISTER_FACTORIES_BEGIN()
FCGIDAEMON_ADD_DEFAULT_FACTORY("cocaine-fastcgi", fastcgi_module_t);
FCGIDAEMON_REGISTER_FACTORIES_END()
