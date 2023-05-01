#pragma once

#ifdef __unix__

#include <iostream>
#include <stdlib.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>
#include <atomic>
#include <vector>
#include <cstring>
#include <thread>
#include <map>
#include <functional>

enum class PipeAccess {
    Read,
    Write,
};

class UnixPipe {
public:
    // Initial (and incremental) buffer size for incoming data
    static size_t const INITIAL_BUFFER_SIZE = 8096;
    // Prefix attached to each message to check for start
    constexpr static const char* const PREFIX = "NAMEDPIPE";
    constexpr static const char* const START = "START";
    constexpr static const char* const END = "END";

    UnixPipe(std::string const name, PipeAccess access) : m_name(name), m_access(access), m_fd(-1), m_hasToStop(false), m_reader() {
        struct stat st;
        // Check if pipe exists
        if (stat(name.c_str(), &st) == 0) {
            if (!S_ISFIFO(st.st_mode)) {
                std::cerr << name << " is not a named pipe." << std::endl;
            }
        } else {
            if (mkfifo(name.c_str(), 0666) == -1) {
                perror("mkfifo");
                abort();
            }
        }
        // Open pipe
        m_fd = open(name.c_str(), (access == PipeAccess::Write) ? O_RDWR | O_NONBLOCK : O_RDWR);
    }

    ~UnixPipe() {
        if (m_reader && m_reader->joinable()) {
            m_hasToStop = true;
            m_reader->join();
        }
    }

    void start() {
        // Check if read access
        if (m_access != PipeAccess::Read) {
            throw std::logic_error("Tried to call start on pipe with write access only.");
        }
        // Start read thread if missing
        if (!m_reader) {
            m_reader.reset(new std::thread(std::bind(&UnixPipe::handleRead, this)));
        }
    }

    void addCallback(std::string const id, std::function<void(std::string const&)> callback) {
        // Check if read access
        if (m_access != PipeAccess::Read) {
            throw std::logic_error("Tried to call start on pipe with write access only.");
        }
        // Check if callback already present
        if (m_callbacks.find(id) != m_callbacks.end()) {
            throw std::logic_error("Tried to add a second callback for the same identifier.");
        }
        m_callbacks[id] = callback;
    }

    void write(std::string id, std::string msg) {
        int totalWritten = 0;
        // Check if write access
        if (m_access != PipeAccess::Write) {
            throw std::logic_error("Tried to call write on pipe with read access only.");
        }
        // Escape all PREFIX in id and msg
        std::string& escapedId = escape(escape(escape(id, PREFIX), START), END);
        std::string& escapedMsg = escape(escape(escape(msg, PREFIX), START), END);
        // Create full message
        std::string fullMsg = std::string(PREFIX) + ":" + std::string(START) + ":" + std::to_string(escapedId.size()) + ":" + std::to_string(escapedMsg.size()) + ":" + escapedId + ":" + escapedMsg + ":" + std::string(END) + ":";
        // Transmit message
        while (totalWritten < msg.length()) {
            int written = ::write(m_fd, &fullMsg.c_str()[totalWritten], fullMsg.length() - totalWritten);
            if (written == -1) {
                perror("write");
                throw std::logic_error("Write to named pipe failed!");
            }
            totalWritten += written;
        }
    }

private:
    std::string m_name;
    PipeAccess m_access;
    int m_fd;
    std::atomic<bool> m_hasToStop;
    std::unique_ptr<std::thread> m_reader;
    std::map<std::string, std::function<void(std::string const&)>> m_callbacks;

    struct PipeMessage {
        std::string id;
        std::string content;
        size_t totalLength;
    };

    PipeMessage nextMessage(std::string const& input, size_t filled) {
        static const std::string prefix = std::string(PREFIX) + ":" + std::string(START) + ":";
        size_t posPrefix = 0;
        // Create empty msg
        PipeMessage msg;
        msg.totalLength = 0;
        // Remove empty if not enough data
        if (filled < prefix.length()) {
            return msg;
        }
        // Check if prefix that marks start of message
        while (true) {
            posPrefix = input.find_first_of(prefix, posPrefix);
            if (posPrefix == std::string::npos) {
                return msg;
            } else if (posPrefix == 0 || (input[posPrefix-1] != '\\')) {
                break;
            }
        }
        // Check if first separator exists
        size_t posEndIdLen = input.find_first_of(':', posPrefix + prefix.length());
        if (posEndIdLen == std::string::npos) {
            return msg;
        }
        // Check if second separator exists
        size_t posEndMsgLen = input.find_first_of(':', posEndIdLen + 1);
        if (posEndMsgLen == std::string::npos) {
            return msg;
        }
        // Retrieve id and message length
        size_t idLen;
        size_t msgLen;
        std::string idLenStr = input.substr(prefix.length(), posEndIdLen - (prefix.length()));
        std::string msgLenStr = input.substr(posEndIdLen + 1, posEndMsgLen - (posEndIdLen + 1));
        // Check if strings can be transformed into numbers
        try {
            idLen = std::stoul(idLenStr);
            msgLen = std::stoul(msgLenStr);
        } catch(std::invalid_argument& ex) {
            return msg;
        }
        // Check if total length is enough
        size_t totalLength = posEndMsgLen + idLen + msgLen + 4 + std::strlen(END);
        if (filled < totalLength || (input.find_first_of(END, totalLength - std::strlen(END) - 1) != (totalLength - std::strlen(END) - 1)) || input[totalLength - 1] != ':') {
            return msg;
        }
        // Extract id and message
        msg.totalLength = totalLength;
        std::string id = input.substr(posEndMsgLen + 1, idLen);
        msg.id = unescape(unescape(unescape(id, PREFIX), START), END);
        std::string content = input.substr(posEndMsgLen + idLen + 2, msgLen);
        msg.content = unescape(unescape(unescape(content, PREFIX), START), END);
        // Return
        return msg;
    }

    std::string& unescape(std::string& str, const char* const tag) {
        size_t pos = 0;
        static size_t const tagLength = std::strlen(tag);
        static std::string const escapedTag = "\\" + std::string(tag);
        // Unescape all tags
        while((pos = str.find(escapedTag, pos)) != std::string::npos) {
            str.replace(pos, tagLength + 1, tag);
            pos += tagLength + 1;
        }
        return str;
    }

    std::string& escape(std::string& str, const char* const tag) {
        size_t pos = 0;
        static size_t const tagLength = std::strlen(tag);
        static std::string const escapedTag = "\\" + std::string(tag);
        // Escape all tags
        while((pos = str.find(tag, pos)) != std::string::npos) {
            str.replace(pos, tagLength, escapedTag.c_str());
            pos += escapedTag.length();
        }
        return str;
    }

    void handleRead() {
        size_t filled = 0;
        std::string input(INITIAL_BUFFER_SIZE, '\0');
        // Run until stopped
        while (!m_hasToStop) {
            // Read data if available
            int read = ::read(m_fd, &input[filled], input.capacity() - filled);
            // Check if some error other than missing writer exists
            if (read == -1 && errno != ENXIO && errno != EAGAIN) {
                throw std::logic_error("Reading from named pipe failed!");
            } else if (read > 0) {
                filled += read;
                // Check buffer for messages
                while (true) {
                    // Check if message if fully read
                    PipeMessage msg = nextMessage(input, filled);
                    // If message is found
                    if (msg.totalLength > 0) {
                        if (m_callbacks.find(msg.id) != m_callbacks.end()) {
                            m_callbacks[msg.id](msg.content);
                        }
                        input = input.substr(msg.totalLength, std::string::npos);
                        filled = filled - msg.totalLength;
                    } else {
                        if (filled == input.size()) {
                            // Increate buffer if no full message is retrieved and input is full
                            input.resize(input.size() + INITIAL_BUFFER_SIZE);
                        }
                        break;
                    }
                }
            }
        }
    }
};

#endif
