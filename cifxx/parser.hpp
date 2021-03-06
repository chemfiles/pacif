// Copyright (c) 2017-2018, Guillaume Fraux
// All rights reserved.
// Redistribution and use in source and binary forms, with or without
// modification, are permitted provided that the following conditions are met:
//
// 1. Redistributions of source code must retain the above copyright
//    notice, this list of conditions and the following disclaimer.
// 2. Redistributions in binary form must reproduce the above copyright
//    notice, this list of conditions and the following disclaimer in the
//    documentation and/or other materials provided with the distribution.
// 3. Neither the name of the copyright holder nor the names of its contributors
//    may be used to endorse or promote products derived from this software
//    without specific prior written permission.
//
// THIS SOFTWARE IS PROVIDED BY THE CONTRIBUTORS "AS IS" AND ANY EXPRESS OR IMPLIED
// WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES OF
// MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT
// SHALL THE CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL,
// EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT
// OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
// INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
// CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING
// IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY
// OF SUCH DAMAGE.

#ifndef CIFXX_PARSER_HPP
#define CIFXX_PARSER_HPP

#include <cassert>
#include <cstddef>
#include <memory>
#include <string>
#include <vector>
#include <istream>
#include <iterator>

#include "types.hpp"
#include "value.hpp"
#include "data.hpp"
#include "token.hpp"
#include "tokenizer.hpp"

namespace cifxx {

class parser final {
public:
    explicit parser(std::string input): tokenizer_(std::move(input)), current_(tokenizer_.next()) {}

    template<typename Stream>
    explicit parser(Stream&& input): parser({
        std::istreambuf_iterator<char>(input),
        std::istreambuf_iterator<char>()
    }) {}

    parser(parser&&) = default;
    parser& operator=(parser&&) = default;

    /// Parse a whole file and get all the data blocks inside
    std::vector<data> parse() {
        std::vector<data> data;
        while (!finished()) {
            data.emplace_back(next());
        }
        return data;
    }

    /// Check whether we have read all the data in the file
    bool finished() const {
        return current_.kind() == token::Eof;
    }

    /// Read a single data block from the file
    data next() {
        if (!check(token::Data)) {
            throw_error(
                "expected 'data_' at the begining of the data block, "
                "got '" + current_.print() + "'"
            );
        }
        auto block = data(advance().as_str_view().to_string());

        while (!finished()) {
            if (check(token::Data)) {
                break;
            } else if (check(token::Tag)) {
                read_tag(block);
            } else if (check(token::Loop)) {
                read_loop(block);
            } else if (check(token::Save)) {
                read_save(block);
            } else {
                throw_error(
                    "expected a tag, a loop or a save frame in data block, "
                    "got '" + current_.print() + "'"
                );
            }
        }

        return block;
    }

private:
    /// Advance the current token by one and return the current token.
    token advance() {
        if (!finished()) {
            auto other = tokenizer_.next();
            std::swap(other, current_);
            return other;
        } else {
            return current_;
        }
    }

    /// Check if the current token have a given kind
    bool check(token::Kind kind) const {
        return current_.kind() == kind;
    }

    /// Read a save frame
    void read_save(data& block) {
        auto name = advance().as_str_view();
        auto save = basic_data();

        while (!finished()) {
            if (check(token::SaveEnd)) {
                advance();
                break;
            } else if (check(token::Tag)) {
                read_tag(save);
            } else if (check(token::Loop)) {
                read_loop(save);
            } else if (check(token::Data)) {
                throw_error("expected end of save frame, got a new data block");
            } else if (check(token::Save)) {
                throw_error("expected end of save frame, got a new save frame");
            } else {
                throw_error("expected a tag, a loop or a save frame in data block, got " + current_.print());
            }
        }

        block.add_save(name.to_string(), std::move(save));
    }

    /// Read a single tag + value
    void read_tag(basic_data& data) {
        auto tag_name = advance().as_tag();

        if (check(token::Dot) || check(token::QuestionMark)) {
            advance();
            data.emplace(tag_name.to_string(), value::missing());
        } else if (check(token::Number)) {
            data.emplace(tag_name.to_string(), advance().as_number());
        } else if (check(token::String)) {
            data.emplace(tag_name.to_string(), advance().as_str_view().to_string());
        } else {
            throw_error("expected a value for tag " + tag_name.to_string() + " , got " + current_.print());
        }
    }

    /// Read a loop section
    void read_loop(basic_data& data) {
        auto loop = advance();
        assert(loop.kind() == token::Loop);

        std::vector<std::pair<std::string, vector_t>> values;
        while (check(token::Tag)) {
            values.emplace_back(advance().as_tag().to_string(), vector_t());
        }

        size_t current = 0;
        while (!finished()) {
            size_t index = current % values.size();
            if (check(token::Dot) || check(token::QuestionMark)) {
                advance();
                values[index].second.emplace_back(value::missing());
            } else if (check(token::Number)) {
                values[index].second.emplace_back(advance().as_number());
            } else if (check(token::String)) {
                values[index].second.emplace_back(advance().as_str_view().to_string());
            } else {
                break;
            }
            current++;
        }

        if (current % values.size() != 0) {
            throw_error(
                "not enough values in the last loop iteration: expected " +
                std::to_string(values.size()) + " got " +
                std::to_string(current % values.size())
            );
        }

        for (auto it: values) {
            data.emplace(std::move(it.first), std::move(it.second));
        }
    }

    [[noreturn]] void throw_error(std::string message) {
        throw error(
            "error on line " + std::to_string(tokenizer_.line()) + ": " + message
        );
    }

    tokenizer tokenizer_;
    token current_;
};

}

#endif
