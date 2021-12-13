#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <unistd.h>
#include <cstdlib>
#include <sstream>
#include <regex>
#include <filesystem>
#include <ext/stdio_filebuf.h>
#include <sys/sendfile.h>
#include <csignal>


#define QUEUE_LENGTH 10

static const std::regex conn_insens("CONNECTION", std::regex_constants::icase);
static const std::regex contlen_insens("CONTENT-LENGTH  ",
                                       std::regex_constants::icase);
static const std::regex conttype_insens("Content-Type",
                                        std::regex_constants::icase);
static const std::regex serv_insens("Server", std::regex_constants::icase);

std::map <std::string, std::string> corr_map;

std::string findInCorreleted(std::filesystem::path &path) {
    auto it = corr_map.find(path.c_str());
    std::string adress;
    if (it != corr_map.end()) {
        std::string serwer_port = (*it).second;
        std::string serwer = serwer_port.substr(0, serwer_port.find('\t'));
        std::string port = serwer_port.substr(serwer_port.find('\t') + 1,
                                              serwer_port.size());
        adress += "http://";
        adress += serwer;
        adress += ':';
        adress += port;
        adress += path.string();
        return adress;
    }

    return ":";
}

std::string path_to_send_file;

int SIGPIPE_FLAG = 0;

void sendReply(std::string &reply, int socket) {
    fprintf(stderr, "%s", reply.c_str());

    if (write(socket, reply.c_str(), reply.length()) == -1) {
        SIGPIPE_FLAG = 1;
        return;
    }
    int wrote = 0;
    if (!path_to_send_file.empty()) {
        FILE *file = fopen(path_to_send_file.c_str(), "r");
        do {
            wrote = sendfile(socket, fileno(file), NULL, 2048);
            if (wrote == -1) {
                SIGPIPE_FLAG = 1;
                return;
            }
        } while (wrote > 0);
        path_to_send_file.clear();
    }
}

bool checkForClose(std::vector <std::string> tokens) {
    for (unsigned long i = 1; i < tokens.size(); i++) {
        std::string field_name = tokens.at(i).substr(0, tokens.at(i).find(':'));
        if (std::regex_match(field_name, conn_insens)) {
            std::vector <std::string> tokens2;
            std::regex e("(:| )+");
            std::regex_token_iterator <std::string::iterator> it(
                    tokens.at(i).begin(), tokens.at(i).end(), e, -1);
            std::regex_token_iterator <std::string::iterator> end;
            while (it != end) {
                tokens2.push_back(*it);
                it++;
            }

            if (tokens2.at(1) == "close") {
                return true;
            } else if (tokens2.at(1) == "open") {
                return false;
            } else {
                printf("Wrong message!.\n");
                return false;
            }
        }
    }
    return false;
}

bool checkPath(std::filesystem::path path) {
    std::vector <std::string> tokens2;
    std::regex e(R"(\/)");
    std::string p = path.string();
    std::regex_token_iterator <std::string::iterator> it(p.begin(), p.end(), e,
                                                         -1);
    std::regex_token_iterator <std::string::iterator> end;
    while (it != end) {
        tokens2.push_back(*it);
        it++;
    }
    int cnt = 0;
    for (auto &s : tokens2) {
        if (s == "..") {
            cnt--;
        } else if (s != "." && !s.empty()) {
            cnt++;
        }
    }
    if (cnt <= 1) {
        return false;
    }
    return true;
}

std::string
replyHead(std::filesystem::path &path_org, std::filesystem::path &resources,
          std::vector <std::string> tokens) {
    std::string replyForHead = "HTTP/1.1 ";
    bool found = false;

    std::ifstream file;
    std::filesystem::path path;
    path = resources.string() + path_org.string();
    std::string headers;
    if (checkPath(path)) { // usun to??
        file.open(path.c_str());
        if (!file.fail()) {
            found = true;
        }

        if (found) {
            if (is_directory(path)) {
                replyForHead += "404  File does not exist";
            } else {
                replyForHead += "200";
                replyForHead += " File found";
                headers += "Content-Type: ";
                headers += "application/octet-stream";
                headers += "\r\n";

                headers += "Content-Length: ";
                headers += std::to_string(file_size(path));
                headers += "\r\n";
            }
        } else {
            std::filesystem::path location;
            auto adress = findInCorreleted(path_org);
            if (adress != ":") {
                replyForHead += "302";
                replyForHead += " Found in Correleted";
                headers += "Location: ";
                headers += adress;
                headers += "\r\n";
            } else {
                replyForHead += "404";
                replyForHead += " File does not exist";
            }
        }
        file.close();

    } else {
        replyForHead += "404";
        replyForHead += " File does not exist";
    }

    replyForHead += "\r\n";
    replyForHead += headers;
    if (checkForClose(tokens)) {
        replyForHead += "Connection: close\r\n";
    }
    replyForHead += "\r\n";

    return replyForHead;
}


std::string
replyGet(std::filesystem::path &path_org, std::filesystem::path &resources,
         std::vector <std::string> tokens) {
    std::string replyForGet = "HTTP/1.1 ";
    bool found = false;

    std::ifstream file;
    std::filesystem::path path;
    path = resources.string() + path_org.string();
    std::string headers;
    bool error404 = false;
    if (checkPath(path)) {
        file.open(path.c_str());
        if (!file.fail()) {
            found = true;
        }

        if (found) {
            if (is_directory(path)) {
                replyForGet += "404  File does not exist";
                error404 = true;
            } else {
                replyForGet += "200";
                replyForGet += " File found";
                headers += "Content-Type: ";
                headers += "application/octet-stream";
                headers += "\r\n";

                headers += "Content-Length: ";
                headers += std::to_string(file_size(path));
                headers += "\r\n";
            }
        } else {
            std::filesystem::path location;
            auto adress = findInCorreleted(path_org);
            if (adress != ":") {
                replyForGet += "302"; // i dotego nagłowek Location!!
                replyForGet += " Found in Correleted";
                headers += "Location: ";
                headers += adress;
                headers += "\r\n";
                replyForGet += "\r\n";
                replyForGet += headers;
                replyForGet += "\r\n";

                return replyForGet;
            } else {
                replyForGet += "404";
                replyForGet += " File does not exist";
                error404 = true;
            }
        }
    } else {
        replyForGet += "404";
        replyForGet += " File does not exist";
        error404 = true;
    }

    replyForGet += "\r\n";
    replyForGet += headers;
    //replyForGet += "\r\n"; // tak dobrze ten CRLF??

    if (checkForClose(tokens)) {
        replyForGet += "Connection: close\r\n";
    }
    replyForGet += "\r\n";

    std::string body;

    if (!error404) {
        path_to_send_file = path.c_str();
        file.close();

    }

    replyForGet += body;

    return replyForGet;
}

bool checkForCopyHeaders(std::vector <std::string> tokens) {
    for (unsigned long i = 1; i < tokens.size(); i++) {
        std::string field_name = tokens.at(i).substr(0, tokens.at(i).find(':'));
        if (std::regex_match(field_name, conn_insens) ||
            std::regex_match(field_name, contlen_insens) ||
            std::regex_match(field_name, conttype_insens) ||
            std::regex_match(field_name, serv_insens)) {

            for (unsigned long j = i + 1; j < tokens.size(); j++) {
                std::string field_name2 = tokens.at(j).substr(0,
                                                              tokens.at(i).find(
                                                                      ':'));
                if (field_name == field_name2) {
                    return true;
                }
            }
        }
    }
    return false;
}

int main(int argc, char *argv[]) {

    if (argc < 3) {
        exit(EXIT_FAILURE);
    }
    int port_num = 8080;
    if (argc == 4) {
        port_num = atoi(argv[3]);
    }

    std::filesystem::path correleted(argv[2]);
    std::filesystem::path resources(argv[1]);

    if (!is_directory(resources) || !exists(resources)) {
        exit(EXIT_FAILURE);
    }
    if (is_directory(correleted) || !exists(correleted)) {
        exit(EXIT_FAILURE);
    }

    std::ifstream correleted_file;
    correleted_file.open(correleted);
    if (!correleted_file.is_open()) {
        exit(EXIT_FAILURE);
    }

    signal(SIGPIPE, SIG_IGN);

    std::string line;
    while (getline(correleted_file, line)) {
        std::string corr_file = line.substr(0, line.find('\t'));// 9- to tab
        std::string serwer_port = line.substr(line.find('\t') + 1, line.size());
        corr_map.insert(
                std::pair<std::string, std::string>(corr_file, serwer_port));
    }
    correleted_file.close();

    int sock, msg_sock;
    struct sockaddr_in server_address;
    struct sockaddr_in client_address;
    socklen_t client_address_len;

    sock = socket(PF_INET, SOCK_STREAM, 0); // creating IPv4 TCP socket
    if (sock < 0)
        exit(EXIT_FAILURE);
    // after socket() call; we should close(sock) on any execution path;
    // since all execution paths exit immediately, sock would be closed when program terminates

    server_address.sin_family = AF_INET; // IPv4
    server_address.sin_addr.s_addr = htonl(
            INADDR_ANY); // listening on all interfaces
    server_address.sin_port = htons(port_num); // listening on port PORT_NUM

    // bind the socket to a concrete address
    if (bind(sock, (struct sockaddr *) &server_address,
             sizeof(server_address)) < 0)
        exit(EXIT_FAILURE);


    // switch to listening (passive open)
    if (listen(sock, QUEUE_LENGTH) < 0)
        exit(EXIT_FAILURE);

    static const std::regex check_msg
            (R"([!#$%&'*+\-.^_`|~\da-zA-Z]+ \/[a-zA-Z0-9.\-\/]* HTTP\/1.1\r\n([^:]+:[ ]*[^:]+[ ]*\r\n)*\r\n)",
             std::regex_constants::optimize);

    for (;;) {
        client_address_len = sizeof(client_address);
        // get client connection from the socket
        msg_sock = accept(sock, (struct sockaddr *) &client_address,
                          &client_address_len);
        if (msg_sock < 0)
            exit(EXIT_FAILURE);

        __gnu_cxx::stdio_filebuf<char> file_buf(msg_sock, std::ios::in);
        std::iostream input_stream(&file_buf);

        do {
            std::string input;
            std::string buffor;

            while (std::getline(input_stream, buffor)) {
                if (buffor == "\r") {
                    buffor += "\n";
                    input += buffor;
                    break;
                } else {
                    buffor += "\n";
                    input += buffor;
                    buffor.clear();
                }
            }

            if (input.empty()) {
                continue;
            }

            std::vector <std::string> tokens;

            std::string reply;

            if (std::regex_match(input.begin(), input.end(), check_msg)) {
                std::string method = input.substr(0, input.find(' '));
                if (method == "HEAD" || method == "GET") {
                    std::regex e("\r\n");
                    std::regex_token_iterator <std::string::iterator> it(
                            input.begin(), input.end(), e, -1);
                    std::regex_token_iterator <std::string::iterator> end;
                    while (it != end) {
                        tokens.push_back(*it);
                        it++;
                    }

                } else {
                    reply = "HTTP/1.1 501 wrong_instruction\r\n";
                }
            } else {
                reply = "HTTP/1.1 400 wrong_format\r\n";
            }

            if (checkForCopyHeaders(tokens)) {
                reply = "HTTP/1.1 400 wrong_format\r\n";

            }
            if (!reply.empty()) {
                if (reply == "HTTP/1.1 400 wrong_format\r\n") {
                    reply += "Connection: close\r\n\r\n";
                    sendReply(reply, msg_sock);

                    break;
                } else {
                    reply += "\r\n";
                    sendReply(reply, msg_sock);
                    continue;
                }
            }

            std::vector <std::string> request;

            std::string temp;

            std::regex e(" ");
            std::regex_token_iterator <std::string::iterator> it(
                    tokens.at(0).begin(), tokens.at(0).end(), e, -1);
            std::regex_token_iterator <std::string::iterator> end;
            while (it != end) {
                request.push_back(*it);
                it++;
            }

            std::string reply2;
            std::filesystem::path path = request.at(1);
            if (request.at(0) == "HEAD") {
                reply2 = replyHead(path, resources, tokens);

            } else if (request.at(0) == "GET") {
                reply2 = replyGet(path, resources, tokens);
            } else {
                reply2 = "HTTP/1.1 501 wrong_instruction\r\n";
                sendReply(reply, msg_sock);
                continue;
            }

            if (checkForClose(tokens)) {
                sendReply(reply2, msg_sock);

                break;
            }
            sendReply(reply2, msg_sock);
            if (SIGPIPE_FLAG == 1) {
                SIGPIPE_FLAG = 0;
                break;
            }
        } while (true);

        if (close(msg_sock) < 0) {
            exit(EXIT_FAILURE);
        }
    }

    return 0;
}


