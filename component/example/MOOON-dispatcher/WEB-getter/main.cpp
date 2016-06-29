/**
 * Licensed to the Apache Software Foundation (ASF) under one or more
 * contributor license agreements.  See the NOTICE file distributed with
 * this work for additional information regarding copyright ownership.
 * The ASF licenses this file to You under the Apache License, Version 2.0
 * (the "License"); you may not use this file except in compliance with
 * the License.  You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 *
 * Author: eyjian@qq.com or eyjian@gmail.com
 */
#include <mooon/sys/logger.h>
#include <mooon/utils/args_parser.h>
#include <mooon/sys/main_template.h>
#include <mooon/dispatcher/dispatcher.h>
#include "getter.h"

/***
  * 使用示例：./web_getter --dn=news.163.com --port=80 --url=/11/0811/00/7B4S5OP50001121M.html
  */

INTEGER_ARG_DEFINE(uint16_t, port, 80, 1, 65535, "web server port")
STRING_ARG_DEFINE(dn, "", "domain name")
STRING_ARG_DEFINE(url, "", "URL")

class CMainHelper: public mooon::sys::IMainHelper
{
public:
    CMainHelper();
    ~CMainHelper();

private:
    virtual bool init(int argc, char* argv[]);
    virtual bool run();
    virtual void fini();

private:
    mooon::sys::CLogger* _logger;
    mooon::dispatcher::IDispatcher* _dispatcher;
};

// 如果main被包含在某个namespace内部，则需要extern "C"修饰
// ，否则链接时，会报main函数未定义，原因是C++编译器会对函数名进行编码
extern "C" int main(int argc, char* argv[])
{
    CMainHelper main_helper;
    return mooon::sys::main_template(&main_helper, argc, argv);
}

CMainHelper::CMainHelper()
    :_dispatcher(NULL)
{
    _logger = new mooon::sys::CLogger;
}

CMainHelper::~CMainHelper()
{
    _logger->destroy();
}

bool CMainHelper::init(int argc, char* argv[])
{    
    // 解析命令行参数
    std::string errmsg;
    if (!mooon::utils::parse_arguments(argc, argv, &errmsg))
    {
        fprintf(stderr, "Command parameter error: %s.\n", errmsg.c_str());
        return false;
    }
    if (0 == mooon::argument::port->value())
    {
        fprintf(stderr, "--port value is zero.\n");
        return false;
    }

    // 确定日志文件存放目录
    // ，在这里将日志文件和程序文件放在同一个目录下
    std::string logdir = mooon::sys::CUtils::get_program_path();

    // 创建日志器，生成的日志文件名为weg_getter.log
    _logger->create(logdir.c_str(), "web_getter.log");

    // 设置日志器
    mooon::dispatcher::logger = _logger;

    // 创建MOOON-dispatcher组件实例
    _dispatcher = mooon::dispatcher::create(2);
    if (NULL == _dispatcher)
    {
        return false;
    }
    
    CGetter::get_singleton()->set_port(mooon::argument::port->value());
    CGetter::get_singleton()->set_domain_name(mooon::argument::dn->value());
    CGetter::get_singleton()->set_url(mooon::argument::url->value());
    CGetter::get_singleton()->set_dispatcher(_dispatcher);

    return true;
}

bool CMainHelper::run()
{
    return CGetter::get_singleton()->start();
}

void CMainHelper::fini()
{
    if (_dispatcher != NULL)
    {
        // 销毁MOOON-dispatcher组件实例
        mooon::dispatcher::destroy(_dispatcher);
        _dispatcher = NULL;
    }
}
