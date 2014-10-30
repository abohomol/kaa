/*
 * Copyright 2014 CyberVision, Inc.
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *      http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#ifndef HTTPCLIENT_HPP_
#define HTTPCLIENT_HPP_

#include "kaa/http/IHttpClient.hpp"
#include <boost/cstdint.hpp>
#include <boost/asio.hpp>
#include <boost/thread/mutex.hpp>

namespace kaa {

class HttpClient : public IHttpClient
{
public:
    HttpClient() : io_(), sock_(io_) { }
    ~HttpClient() { }

    virtual boost::shared_ptr<IHttpResponse> sendRequest(const IHttpRequest& request);
    virtual void closeConnection();

private:
    void checkError(const boost::system::error_code& code);
    void doSocketClose();

private:
    boost::asio::io_service io_;
    boost::asio::ip::tcp::socket sock_;
    boost::mutex guard_;
};

}



#endif /* HTTPCLIENT_HPP_ */