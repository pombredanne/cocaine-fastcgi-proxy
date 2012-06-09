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
                        cookie_names,
                        argument_names,
                        argument_values,
                        file_names;

        packer.pack_map(2);

        // Metadata
        // --------

        packer.pack(std::string("meta"));
        
        packer.pack_map(6);

        packer.pack(std::string("secure"));
        packer.pack(request.isSecure());

        packer.pack(std::string("url"));
        packer.pack(request.getUrl());

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

        // Query arguments
        // ---------------
        
        packer.pack(std::string("request"));
        
        request.remoteFiles(file_names);
        
        packer.pack_map(request.countArgs() + file_names.size());

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
       
        // Uploads
        // -------

        std::string contents;
        
        for(string_vector_t::const_iterator it = file_names.begin();
            it != file_names.end();
            ++it)
        {
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

void fastcgi_module_t::handleRequest(fastcgi::Request * request, fastcgi::HandlerContext * context) {
	(void)context;

    std::string name(request->getScriptName());
    
    if(name.compare(0, sizeof("/ping") - 1, "/ping") == 0) {
        request->setStatus(200);
        request->setContentType("text/plain");
        request->write("ok", sizeof("ok") - 1);
        return;
    }

    boost::shared_ptr<response> future;
    message_path path(make_path(name));

    message_policy mp;
    mp.max_retries = -1;
    mp.deadline = 0.3;

    try {
        future = m_client->send_message(*request, path, mp);
    } catch(const dealer_error& e) {
        log()->error(
            "unable to send message to '%s/%s' - %s",
            path.service_name.c_str(),
            path.handle_name.c_str(),
            e.what()
        );

        throw fastcgi::HttpException(e.code());
    } catch(const internal_error& e) {
        log()->error(
            "unable to send message to '%s/%s' - %s",
            path.service_name.c_str(),
            path.handle_name.c_str(),
            e.what()
        );
        
        throw fastcgi::HttpException(400);
    }

    request->setStatus(200);
    
    if(request->getRequestMethod() == "HEAD") {
        return;
    }
        
    try {
        data_container chunk;
        
        // XXX: Set the content type according to the response's meta.
        request->setContentType("text/plain");

        while(future->get(&chunk)) {
            request->write(
                static_cast<const char*>(chunk.data()),
                chunk.size()
            );
        }
    } catch(const dealer_error& e) { 
        log()->error(
            "unable to process message for '%s/%s' - %s",
            path.service_name.c_str(),
            path.handle_name.c_str(),
            e.what()
        );
        
        throw fastcgi::HttpException(e.code());
    } catch(const internal_error& e) {
        log()->error(
            "unable to process message for '%s/%s' - %s",
            path.service_name.c_str(),
            path.handle_name.c_str(),
            e.what()
        );

        throw fastcgi::HttpException(400);
    } catch(const fastcgi::HttpException &e) {
        throw;
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
