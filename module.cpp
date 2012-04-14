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

fastcgi_module_t::fastcgi_module_t(fastcgi::ComponentContext * context) :
	fastcgi::Component(context),
	m_logger(NULL),
    m_client(NULL)
{ }

fastcgi_module_t::~fastcgi_module_t() {
    m_client.reset();
}

namespace msgpack {
    template<class Stream>
    inline packer<Stream>& operator << (packer<Stream>& packer, const fastcgi::Request& request) {
        typedef std::vector<
            std::string
        > string_vector_t;

        string_vector_t header_names,
                        cookie_names;

        packer.pack_map(2);

        // Metadata
        // --------

        packer.pack(std::string("meta"));
        
        packer.pack_map(5);

        packer.pack(std::string("secure"));
        packer.pack(request.isSecure());

        packer.pack(std::string("host"));
        packer.pack(request.getHost());
        
        packer.pack(std::string("method"));
        packer.pack(request.getRequestMethod());

        packer.pack(std::string("headers"));
        packer.pack_map(request.countHeaders());

        request.headerNames(header_names);
        
        for(string_vector_t::const_iterator it = header_names.begin();
            it != header_names.end();
            ++it)
        {
            packer.pack(*it);
            packer.pack(request.getHeader(*it));
        }

        packer.pack(std::string("cookies"));
        packer.pack_map(request.countCookie());

        request.cookieNames(cookie_names);
        
        for(string_vector_t::const_iterator it = cookie_names.begin();
            it != cookie_names.end();
            ++it)
        {
            packer.pack(*it);
            packer.pack(request.getCookie(*it));
        }

        // Request
        // -------
        
        packer.pack(std::string("request"));
        
        if(request.getRequestMethod() == "GET") {
            string_vector_t argument_names,
                            argument_values;

            packer.pack_map(request.countArgs());

            request.argNames(argument_names);

            for(string_vector_t::const_iterator it = argument_names.begin();
                it != argument_names.end();
                ++it)
            {
                request.getArg(*it, argument_values);

                packer.pack(*it);

                if(argument_values.size() == 1) {
                    packer.pack(argument_values[0]);
                } else {
                    packer.pack(argument_values);
                }
            }
        } else if(request.getRequestMethod() == "POST") {
            std::string body;
            
            request.requestBody().toString(body);
            
            packer.pack(body);
        } else {
            throw fastcgi::HttpException(400);
        }

        return packer;
    }
}

void fastcgi_module_t::handleRequest(fastcgi::Request * request, fastcgi::HandlerContext * context) {
	(void)context;

    boost::shared_ptr<response> future;
    
    message_path path(
        make_path(request->getScriptName()));

    try {
        future = m_client->send_message(
            *request,
            path,
            message_policy());
    } catch(const error& e) {
        log()->error(
            "unable to send message for service: %s, handle: %s - %s",
            path.service_name.c_str(),
            path.handle_name.c_str(),
            e.what());
        throw fastcgi::HttpException(e.type());
    }

    try {
        data_container chunk;
        
        request->setStatus(200);
        request->setContentType("text/plain");

        while(future->get(&chunk)) {
            request->write(
                static_cast<const char*>(chunk.data()),
                chunk.size());
        }
    } catch(const error& e) { 
        log()->error(
            "unable to process message for service: %s, handle: %s - %s",
            path.service_name.c_str(),
            path.handle_name.c_str(),
            e.what());
        throw fastcgi::HttpException(e.type());
	} catch(const fastcgi::HttpException &e) {
		throw;
	} catch(...) {
		throw fastcgi::HttpException(400);
    }
}

message_path fastcgi_module_t::make_path(const std::string& script_name) const {
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

    if(tokens.size() != 2) {
        log()->error(
            "invalid message path, got %d path components",
            tokens.size()
        );

        throw fastcgi::HttpException(400);
    }

    return message_path(tokens[0], tokens[1]);
}

void fastcgi_module_t::onLoad() {
	assert(NULL == m_logger);

	const fastcgi::Config * config = context()->getConfig();
	std::string path(context()->getComponentXPath());

	m_logger = context()->findComponent<fastcgi::Logger>(config->asString(path + "/logger"));

	if(!m_logger) {
		throw std::logic_error("can't find logger");
	}

    std::string config_path(config->asString(path + "/client/configuration"));
    m_client.reset(new client(config_path));
}

void fastcgi_module_t::onUnload() {
    m_client.reset();
}

FCGIDAEMON_REGISTER_FACTORIES_BEGIN()
FCGIDAEMON_ADD_DEFAULT_FACTORY("cocaine-fastcgi", fastcgi_module_t);
FCGIDAEMON_REGISTER_FACTORIES_END()
