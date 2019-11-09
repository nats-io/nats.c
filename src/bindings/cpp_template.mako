// This file has been auto-generated.
// DO NOT EDIT UNLESS YOU ARE SURE THAT YOU KNOW WHAT YOU ARE DOING

// Copyright 2015-2020 The NATS Authors
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
// http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

#ifndef NATS_HPP_
#define NATS_HPP_

#include "nats.h"
#include <exception>
% for ns in namespaces:

% if ns.include_guard:
#if ${ns.include_guard}
% endif
namespace ${ns.name} {
% for c in ns.classes:
class ${c.name};
% endfor
% for ftd in ns.function_typedefs:

${ftd.raw_comment}
template<typename T>
using ${ftd.name} = ${ftd.result_type} (T::*)(${ftd.parameters});

template<typename T, ${ftd.name}<T> callback> ${ftd.result_type}
${ftd.name}Callback(${ftd.callbackParameters});
% endfor
% if ns.include_guard:
using nats::Exception;
% else:

class Exception : public std::exception {
    natsStatus status;

public:
    Exception(natsStatus s) : status(s)
    {
    }

    const natsStatus&
    code()
    {
      return status;
    }

    const char *
    what() const noexcept
    {
      return natsStatus_GetText(status);
    }

    static void
    CheckResult(natsStatus status)
    {
        if (status != NATS_OK)
            throw Exception(status);
    }
};
% endif
% for c in ns.classes:

${c.raw_comment}
class ${c.name} {
    class WithoutDestuction;
% for fc in ns.classes:
% if fc.name != c.name:
    friend class ${fc.name};
% endif
% endfor
% for ftd in ns.function_typedefs:
    template<typename T, ${ftd.name}<T> callback> friend ${ftd.result_type}
    ${ftd.name}Callback(${ftd.callbackParameters});
% endfor
    ${c.original_type} * self;

    void
    disableDestroy()
    {
        self = nullptr;
    }

public:
    explicit ${c.name}(${c.original_type}* ptr) : self(ptr)
    {
    }

    ${c.name}(${c.name}&& other) : self(other.Release())
    {
    }

% for m in c.methods:
    /** \brief ${m.brief_comment}
    *
    * @see #${m.original_function}()
    */
% if m.is_constructor:
    ${c.name}(${m.parameters});
% elif m.is_destructor:
    ~${c.name}(${m.parameters});
% else:
    ${m.result_type}
    ${m.name}(${m.parameters})${m.suffix};
% if m.template_parameter:

    template<${m.template_parameter}> ${m.result_type}
    ${m.name}(${m.forward_parameters});
%endif
% endif

%endfor
    operator const ${c.original_type} * () const
    {
        return self;
    }

    operator ${c.original_type} * ()
    {
        return self;
    }

    [[nodiscard]] ${c.original_type} *
    Release()
    {
        ${c.original_type} * ret = self;
        self = nullptr;
        return ret;
    }
};

class ${c.name}::WithoutDestuction : public ${c.name} {
public:
    WithoutDestuction(${c.original_type} * ptr) : ${c.name}(ptr)
    {
    }

    ~WithoutDestuction()
    {
        disableDestroy();
    }
};
% endfor
% for f in ns.functions:

/** \brief ${f.brief_comment}
*
* @see #${f.original_function}()
*/
inline ${f.result_type}
${f.name}(${f.parameters})
{
    return ${f.invocation};
}
% endfor

% for c in ns.classes:
% for m in c.methods:
% if m.is_constructor:
inline ${c.name}::${c.name}(${m.parameters})
{
    ${m.invocation};
% elif m.is_destructor:
inline ${c.name}::~${c.name}(${m.parameters})
{
    ${m.invocation};
% else:
inline ${m.result_type}
${c.name}::${m.name}(${m.parameters})${m.suffix}
{
% if m.temporary_object:
    ${m.temporary_object} ret(nullptr);
    ${m.invocation};
    return ret;
% else:
    return ${m.invocation};
% endif
% endif
}
% if m.template_parameter:

template<${m.template_parameter}> inline ${m.result_type}
${c.name}::${m.name}(${m.forward_parameters})
{
    return ${m.name}(${m.forward_arguments});
}
% endif

%endfor
%endfor
% for ftd in ns.function_typedefs:
template<typename T, ${ftd.name}<T> callback> ${ftd.result_type}
${ftd.name}Callback(${ftd.callbackParameters})
{
% for w in ftd.wrapper:
    ${w};
% endfor
    T * self = static_cast<T *>(closure);
    return (self->*callback)(${ftd.arguments});
}

% endfor
} // namespace ${ns.name}
% if ns.include_guard:
#endif
% endif
% endfor

#endif /* NATS_HPP_ */
