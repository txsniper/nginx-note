                HttpEchoModule学习
一. 框架:
ngx_http_echo_module.h ngx_http_echo_module.c
这两个文件包含了模块的整体框架:

定义：
    Echo模块支撑很多命令(echo, echo_duplicate, echo_flush等等)，
ngx_http_echo_module.h中将每个命令定义为一个opcode(ngx_http_echo_opcode_t)，
ngx_http_echo_cmd_category_t将所有的命令分为两类: (1). echo_handler_cmd,
(2). echo_filter_cmd, 前者表示命令是一个自己独立的执行行为，后者则表示
命令用于对其执行行为的结果进行过滤。
    Echo模块的命令支持处理nginx配置文件定义的脚本变量:

    ngx_http_echo_cmd_t定义了命令和参数列表
    typedef struct {
        ngx_http_echo_opcode_t      opcode;

        /* each argument is of type echo_arg_template_t: */
        ngx_array_t                 *args;
    } ngx_http_echo_cmd_t;

    ngx_http_echo_arg_template_t定义了命令的参数类型，lengths和
values是为了处理nginx变量存在的。
    typedef struct {
        /* holds the raw string of the argument value */
        ngx_str_t       raw_value;
        /* fields "lengths" and "values" are set by
        * the function ngx_http_script_compile,
        * iff the argument value indeed contains
        * nginx variables like "$foo" */
        ngx_array_t     *lengths;
        ngx_array_t     *values;
    } ngx_http_echo_arg_template_t;


执行流程：
    1.  命令的set函数(例如：ngx_http_echo_echo, ngx_http_echo_echo_sleep等等)调用
        ngx_http_echo_helper函数
    2.  ngx_http_echo_helper函数根据命令的类型注册处理handler(ngx_http_echo_handler)，
        并且调用nginx的脚本函数(ngx_http_script_variables_count, ngx_http_script_compile)保存
        和处理命令参数
    3.  当服务端收到客户端请求，执行命令时调用ngx_http_echo_handler，handler主要调用
        ngx_http_echo_run_cmds来根据具体的命令执行响应的函数
        ngx_http_echo_run_cmds主要完成两个工作：
        3.1. 解析出要执行的命令参数值(包括求出脚本变量的值和分离出参数选项)
        3.2. 根据命令的opcode执行具体的函数 


相关知识点：
    1.  ngx_http_clear_content_length和ngx_http_clear_accept_ranges的调用：
        ngx_http_clear_content_length函数清除响应头部中的content_length字段，
            ngx_http_clear_accept_ranges函数清除响应头部中的accept_range字段

            Q. ngx_http_clear_content_length 和 ngx_http_clear_accept_ranges 起到了什 么作用，都有哪些场合需要使用？

            A. 这两个函数用于确定无法获知响应的实际长度时，告诉其它 filter 对响应做相应的 处理。具体分析如下：
                ngx_http_clear_content_length 清空响应包头中 Content-Length 相关的字段：

                    r->headers_out.content_length_n = -1; if (r->headers_out.content_length) 
                    { r->headers_out.content_length->hash = 0; r->headers_out.content_length = NULL; }

                ngx_http_clear_accept_ranges 清空响应包头中和 Accept-Ranges 相关的字段：

                    r->allow_ranges = 0; if (r->headers_out.accept_ranges) 
                    { r->headers_out.accept_ranges->hash = 0; r->headers_out.accept_ranges = NULL; }

                ngx_http_range_filter_module 模块由 accept_ranges 向响应包头填充 Accept-Ranges 字段 
                (告知客户端是否可以请求资源的某个范围内的数据)。动态生成的资源，是不支持通过范围方式请求的。

                content_length_n 标识 Nginx 明确知道请求资源的长度。动态生成的资源，无法获知准确长度的情况下，
                需要使用 传输编码 对整个报文进行编码，即 Transfer-Encoding。目前最新的 HTTP 规范只定义了一种传输编码，
                chunked。 ngx_http_chunked_filter_module 负责根据 content_length_n 的值判断是否 添加 Transfer-Encoding 
                包头 (设置 r->chunked，由 header_filter 添加)。

            content_length的影响：
                对于http1.0协议来说，如果响应头中有content-length头，则以content-length的长度就可以知道body的长度了，
            客户端在接收body时，就可以依照这个长度来接收数据，接收完后，就表示这个请求完成了。而如果没有content-length头，
            则客户端会一直接收数据，直到服务端主动断开连接，才表示body接收完了。

                而对于http1.1协议来说，如果响应头中的Transfer-encoding为chunked传输，则表示body是流式输出，body会被分
            成多个块，每块的开始会标识出当前块的长度，此时，body不需要通过长度来指定。如果是非chunked传输，而且有
            content-length，则按照content-length来接收数据。否则，如果是非chunked，并且没有content-length，则客户端
            接收数据，直到服务端主动断开连接。


二. 各种指令的功能：

echo

syntax: echo [options] <string>...
default: no
context: location, location if
phase: content
Sends arguments joined by spaces, along with a trailing newline, out to the client.
Note that the data might be buffered by Nginx's underlying buffer. To force the output data flushed immediately, 
use the echo_flush command just after echo, as in

   echo hello world;
   echo_flush;
When no argument is specified, echo emits the trailing newline alone, just like the echo command in shell.

Variables may appear in the arguments. An example is

   echo The current request uri is $request_uri;
where $request_uri is a variable exposed by the ngx_http_core_module.

This command can be used multiple times in a single location configuration, as in

location /echo {
    echo hello;
    echo world;
}
The output on the client side looks like this

$ curl 'http://localhost/echo'
hello
world

Special characters like newlines (\n) and tabs (\t) can be escaped using C-style escaping sequences. But a notable exception is the dollar sign ($). 
As of Nginx 0.8.20, there's still no clean way to esacpe this characters. (A work-around might be to use a $echo_dollor variable that is always 
evaluated to the constant $ character. This feature will possibly be introduced in a future version of this module.)

As of the echo v0.28 release, one can suppress the trailing newline character in the output by using the -n option, as in

location /echo {
    echo -n "hello, ";
    echo "world";
}
Accessing /echo gives

$ curl 'http://localhost/echo'
hello, world
Leading -n in variable values won't take effect and will be emitted literally, as in

location /echo {
    set $opt -n;
    echo $opt "hello,";
    echo "world";
}
This gives the following output

$ curl 'http://localhost/echo'
-n hello,
world
One can output leading -n literals and other options using the special -- option like this

location /echo {
    echo -- -n is an option;
}
which yields

$ curl 'http://localhost/echo'
-n is an option
