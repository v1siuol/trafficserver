/** @file

  A brief file description

  @section license License

  Licensed to the Apache Software Foundation (ASF) under one
  or more contributor license agreements.  See the NOTICE file
  distributed with this work for additional information
  regarding copyright ownership.  The ASF licenses this file
  to you under the Apache License, Version 2.0 (the
  "License"); you may not use this file except in compliance
  with the License.  You may obtain a copy of the License at

      http://www.apache.org/licenses/LICENSE-2.0

  Unless required by applicable law or agreed to in writing, software
  distributed under the License is distributed on an "AS IS" BASIS,
  WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
  See the License for the specific language governing permissions and
  limitations under the License.
 */

/*****************************************************************************
 *
 *  ControlBase.cc - Base class to process generic modifiers to
 *                         ControlMatcher Directives
 *
 *
 ****************************************************************************/


#include "ink_platform.h"
#include "ink_port.h"
#include "ink_time.h"
#include "ink_unused.h"        /* MAGIC_EDITING_TAG */

#include "Main.h"
#include "URL.h"
#include "Tokenizer.h"
#include "ControlBase.h"
#include "MatcherUtils.h"
#include "HTTP.h"
#include "ControlMatcher.h"
#include "HdrUtils.h"

#include "tsconfig/TsBuffer.h"

/** Used for printing IP address.
    @code
    uint32_t addr; // IP address.
    printf("IP address = " TS_IP_PRINTF_CODE,TS_IP_OCTETS(addr));
    @endcode
    @internal Need to move these to a common header.
 */
# define TS_IP_OCTETS(x) \
  reinterpret_cast<unsigned char const*>(&(x))[0],   \
    reinterpret_cast<unsigned char const*>(&(x))[1], \
    reinterpret_cast<unsigned char const*>(&(x))[2], \
    reinterpret_cast<unsigned char const*>(&(x))[3]

// ----------
ControlBase::Modifier::~Modifier() {}
ControlBase::Modifier::Type ControlBase::Modifier::type() const { return MOD_INVALID; }
// --------------------------
namespace {
// ----------
struct TimeMod : public ControlBase::Modifier {
  time_t start_time;
  time_t end_time;

  static char const * const NAME;

  virtual Type type() const;
  virtual char const * name() const;
  virtual bool check(HttpRequestData* req) const;
  virtual void print(FILE* f) const;
  static TimeMod* make(char * value, char const ** error);
  static const char* timeOfDayToSeconds(const char *time_str, time_t * seconds);
};

char const * const TimeMod::NAME = "Time";
ControlBase::Modifier::Type TimeMod::type() const { return MOD_TIME; }
char const * TimeMod::name() const { return NAME; }

void TimeMod::print(FILE* f) const {
  fprintf(f, "%s=%ld-%ld  ",
    // Have to cast because time_t can be 32 or 64 bits and the compiler
    // will barf if format code doesn't match.
    this->name(), static_cast<long>(start_time), static_cast<long>(end_time)
  );
}
bool TimeMod::check(HttpRequestData* req) const {
  struct tm cur_time;
  time_t timeOfDay = req->xact_start;
  // Use this to account for daylight savings time.
  ink_localtime_r(&timeOfDay, &cur_time);
  timeOfDay = cur_time.tm_hour*(60 * 60) + cur_time.tm_min*60 + cur_time.tm_sec;
  return start_time <= timeOfDay && timeOfDay <= end_time;
}

TimeMod*
TimeMod::make(char * value, char const ** error) {
  Tokenizer rangeTok("-");
  TimeMod* mod = 0;
  TimeMod tmp;
  int num_tok;

  num_tok = rangeTok.Initialize(value, SHARE_TOKS);
  if (num_tok == 1) {
    *error = "End time not specified";
  } else if (num_tok > 2) {
    *error = "Malformed time range";
  } else if (
    0 == (*error = timeOfDayToSeconds(rangeTok[0], &tmp.start_time))
    && 0 == (*error = timeOfDayToSeconds(rangeTok[1], &tmp.end_time))
  ) {
    mod = new TimeMod(tmp);
  }
  return mod;
}
/**   Converts TimeOfDay (TOD) to second value.
      @a *seconds is set to number of seconds since midnight
      represented by @a time_str.

      @return 0 on success, static error string on failure.
*/
const char *
TimeMod::timeOfDayToSeconds(const char *time_str, time_t * seconds) {
  int hour = 0;
  int min = 0;
  int sec = 0;
  time_t tmp = 0;

  // coverity[secure_coding]
  if (sscanf(time_str, "%d:%d:%d", &hour, &min, &sec) != 3) {
    // coverity[secure_coding]
    if (sscanf(time_str, "%d:%d", &hour, &min) != 2) {
      return "Malformed time specified";
    }
  }

  if (!(hour >= 0 && hour <= 23)) return "Illegal hour specification";

  tmp = hour * 60;

  if (!(min >= 0 && min <= 59)) return "Illegal minute specification";

  tmp = (tmp + min) * 60;

  if (!(sec >= 0 && sec <= 59)) return "Illegal second specification";

  tmp += sec;

  *seconds = tmp;
  return 0;
}

// ----------
struct PortMod : public ControlBase::Modifier {
  int start_port;
  int end_port;

  static char const * const NAME;

  virtual char const * name() const;
  virtual bool check(HttpRequestData* req) const;
  virtual void print(FILE* f) const;

  static PortMod* make(char* value, char const **error);
};

char const * const PortMod::NAME = "Port";
char const * PortMod::name() const { return NAME; }

void PortMod::print(FILE* f) const {
  fprintf(f, "%s=%d-%d  ", this->name(), start_port, end_port);
}

bool PortMod::check(HttpRequestData* req) const {
  int port = req->hdr->port_get();
  return start_port <= port && port <= end_port;
}

PortMod*
PortMod::make(char* value, char const ** error) {
  Tokenizer rangeTok("-");
  PortMod tmp;
  int num_tok = rangeTok.Initialize(value, SHARE_TOKS);

  *error = 0;
  if (num_tok > 2) {
    *error = "Malformed Range";
    // coverity[secure_coding]
  } else if (sscanf(rangeTok[0], "%d", &tmp.start_port) != 1) {
    *error = "Invalid start port";
  } else if (num_tok == 2) {
    // coverity[secure_coding]
    if (sscanf(rangeTok[1], "%d", &tmp.end_port) != 1)
      *error = "Invalid end port";
    else if (tmp.end_port < tmp.start_port)
      *error = "Malformed Range: end port < start port";
  } else {
    tmp.end_port = tmp.start_port;
  }

  // If there's an error message, return null.
  // Otherwise create a new item and return it.
  return *error ? 0 : new PortMod(tmp);
}

// ----------
struct IPortMod : public ControlBase::Modifier {
  int _port;

  static char const * const NAME;

  IPortMod(int port);

  virtual char const * name() const;
  virtual bool check(HttpRequestData* req) const;
  virtual void print(FILE* f) const;
  static IPortMod* make(char* value, char const ** error);
};

char const * const IPortMod::NAME = "IPort";
IPortMod::IPortMod(int port) : _port(port) {}
char const * IPortMod::name() const { return NAME; }

void IPortMod::print(FILE* f) const {
  fprintf(f, "%s=%d  ", this->name(), _port);
}
bool IPortMod::check(HttpRequestData* req) const {
  return req->incoming_port == _port;
}

IPortMod*
IPortMod::make(char* value, char const ** error) {
  IPortMod* zret = 0;
  int port;
  // coverity[secure_coding]
  if (sscanf(value, "%u", &port) == 1) {
    zret = new IPortMod(port);
  } else {
    *error = "Invalid incoming port";
  }
  return zret;
}
// ----------
struct SrcIPMod : public ControlBase::Modifier {
  // Stored in host order because that's how they are compared.
  ip_addr_t start_addr; ///< Start address in HOST order.
  ip_addr_t end_addr; ///< End address in HOST order.

  static char const * const NAME;

  virtual Type type() const;
  virtual char const * name() const;
  virtual bool check(HttpRequestData* req) const;
  virtual void print(FILE* f) const;
  static SrcIPMod* make(char * value, char const ** error);
};

char const * const SrcIPMod::NAME = "SrcIP";
ControlBase::Modifier::Type SrcIPMod::type() const { return MOD_SRC_IP; }
char const * SrcIPMod::name() const { return NAME; }

void SrcIPMod::print(FILE* f) const {
  ip_addr_t a1 = htonl(start_addr);
  ip_addr_t a2 = htonl(end_addr);
  fprintf(f, "%s=%d.%d.%d.%d-%d.%d.%d.%d  ",
    this->name(), TS_IP_OCTETS(a1), TS_IP_OCTETS(a2)
  );
}
bool SrcIPMod::check(HttpRequestData* req) const {
  // Compare in host order
  uint32_t addr = ntohl(req->src_ip);
  return start_addr <= addr && addr <= end_addr;
}
SrcIPMod*
SrcIPMod::make(char * value, char const ** error ) {
  SrcIPMod tmp;
  SrcIPMod* zret = 0;
  *error = ExtractIpRange(value, &tmp.start_addr, &tmp.end_addr);

  if (!*error) zret = new SrcIPMod(tmp);
  return zret;
}
// ----------
struct SchemeMod : public ControlBase::Modifier {
  int _scheme; ///< Tokenized scheme.

  static char const * const NAME;

  SchemeMod(int scheme);

  virtual Type type() const;
  virtual char const * name() const;
  virtual bool check(HttpRequestData* req) const;
  virtual void print(FILE* f) const;

  char const* getWksText() const;

  static SchemeMod* make(char * value, char const ** error);
};

char const * const SchemeMod::NAME = "Scheme";

SchemeMod::SchemeMod(int scheme) : _scheme(scheme) {}

ControlBase::Modifier::Type SchemeMod::type() const { return MOD_SCHEME; }
char const * SchemeMod::name() const { return NAME; }
char const *
SchemeMod::getWksText() const {
  return hdrtoken_index_to_wks(_scheme);
}

bool SchemeMod::check(HttpRequestData* req) const {
  return req->hdr->url_get()->scheme_get_wksidx() == _scheme;
}
void SchemeMod::print(FILE* f) const {
  fprintf(f, "%s=%s  ", this->name(), hdrtoken_index_to_wks(_scheme));
}
SchemeMod*
SchemeMod::make(char * value, char const ** error) {
  SchemeMod* zret = 0;
  int scheme = hdrtoken_tokenize(value, strlen(value));
  if (scheme < 0) {
    *error = "Unknown scheme";
  } else {
    zret = new SchemeMod(scheme);
  }
  return zret;
}
// ----------
// This is a base class for all of the mods that have a
// text string.
struct TextMod : public ControlBase::Modifier {
  ts::Buffer text;

  TextMod();
  ~TextMod();

  // Calls name() which the subclass must provide.
  virtual void print(FILE* f) const;
};
void TextMod::print(FILE* f) const {
  fprintf(f, "%s=%*s  ", this->name(), static_cast<int>(text.size()), text.data());
}

TextMod::TextMod() : text(0,0) {}
TextMod::~TextMod() {
  if (text.data()) free(text.data());
}

// ----------
struct MethodMod : public TextMod {
  static char const * const NAME;

  virtual Type type() const;
  virtual char const * name() const;
  virtual bool check(HttpRequestData* req) const;

  static MethodMod* make(char * value, char const ** error);
};
char const * const MethodMod::NAME = "Method";
ControlBase::Modifier::Type MethodMod::type() const { return MOD_METHOD; }
char const * MethodMod::name() const { return NAME; }
bool MethodMod::check(HttpRequestData* req) const {
  int method_len;
  char const* method = req->hdr->method_get(&method_len);
  return method_len >= static_cast<int>(text.size())
    && 0 == strncasecmp(method, text.data(), text.size())
    ;
}
MethodMod*
MethodMod::make(char * value, char const **) {
  MethodMod* mod = new MethodMod;
  mod->text.set(xstrdup(value), strlen(value));
  return mod;
}

// ----------
struct PrefixMod : public TextMod {
  static char const * const NAME;

  virtual Type type() const;
  virtual char const * name() const;
  virtual bool check(HttpRequestData* req) const;
  static PrefixMod* make(char * value, char const ** error);
};

char const * const PrefixMod::NAME = "Prefix";
ControlBase::Modifier::Type PrefixMod::type() const { return MOD_PREFIX; }
char const * PrefixMod::name() const { return NAME; }
bool PrefixMod::check(HttpRequestData* req) const {
  int path_len;
  char const* path = req->hdr->url_get()->path_get(&path_len);
  bool zret = path_len >= static_cast<int>(text.size())
    && 0 == memcmp(path, text.data(), text.size())
    ;
/*
  Debug("cache_control", "Prefix check: URL=%0.*s Mod=%0.*s Z=%s",
    path_len, path, text.size(), text.data(),
    zret ? "Match" : "Fail"
  );
*/
  return zret;
}
PrefixMod*
PrefixMod::make(char * value, char const ** error ) {
  PrefixMod* mod = new PrefixMod;
  // strip leading slashes because get_path which is used later
  // doesn't include them from the URL.
  while ('/' == *value) ++value;
  mod->text.set(xstrdup(value), strlen(value));
  return mod;
}
// ----------
struct SuffixMod : public TextMod {
  static char const * const NAME;

  virtual Type type() const;
  virtual char const * name() const;
  virtual bool check(HttpRequestData* req) const;
  static SuffixMod* make(char * value, char const ** error);
};
char const * const SuffixMod::NAME = "Suffix";
ControlBase::Modifier::Type SuffixMod::type() const { return MOD_SUFFIX; }
char const * SuffixMod::name() const { return NAME; }
bool SuffixMod::check(HttpRequestData* req) const {
  int path_len;
  char const* path = req->hdr->url_get()->path_get(&path_len);
  return path_len >= static_cast<int>(text.size())
    && 0 == strncasecmp(path + path_len - text.size(), text.data(), text.size())
    ;
}
SuffixMod*
SuffixMod::make(char * value, char const ** error ) {
  SuffixMod* mod = new SuffixMod;
  mod->text.set(xstrdup(value), strlen(value));
  return mod;
}

// ----------
struct TagMod : public TextMod {
  static char const * const NAME;

  virtual Type type() const;
  virtual char const * name() const;
  virtual bool check(HttpRequestData* req) const;
  static TagMod* make(char * value, char const ** error);
};
char const * const TagMod::NAME = "Tag";
ControlBase::Modifier::Type TagMod::type() const { return MOD_TAG; }
char const * TagMod::name() const { return NAME; }
bool TagMod::check(HttpRequestData* req) const {
  return 0 == strcmp(req->tag, text.data());
}
TagMod*
TagMod::make(char * value, char const ** error ) {
  TagMod* mod = new TagMod;
  mod->text.set(xstrdup(value), strlen(value));
  return mod;
}
// ----------
} // anon name space
// ------------------------------------------------
ControlBase::~ControlBase() {
  this->clear();
}

void
ControlBase::clear() {
  line_num = 0;
  for ( Array::iterator spot = _mods.begin(), limit = _mods.end()
      ; spot != limit
      ; ++spot
  )
    delete *spot;
  _mods.clear();
}

// static const modifier_el default_el = { MOD_INVALID, NULL };

void
ControlBase::Print() {
  int n = _mods.size();

  if (0 >= n) return;

  printf("\t\t\t");
  for (intptr_t i = 0; i < n; ++i) {
    Modifier* cur_mod = _mods[i];
    if (!cur_mod) printf("INVALID  ");
    else cur_mod->print(stdout);
  }
  printf("\n");
}

char const *
ControlBase::getSchemeModText() const {
  char const* zret = 0;
  Modifier* mod = this->findModOfType(Modifier::MOD_SCHEME);
  if (mod) zret = static_cast<SchemeMod*>(mod)->getWksText();
  return zret;
}

bool
ControlBase::CheckModifiers(HttpRequestData * request_data) {
  if (!request_data->hdr) {
    //we use the same request_data for Socks as well (only IpMatcher)
    //we just return false here
    return true;
  }

  // If the incoming request has no tag but the entry does, or both
  // have tags that do not match, then we do NOT have a match.
  if (!request_data->tag && findModOfType(Modifier::MOD_TAG))
    return false;

  for (int i = 0, n = _mods.size() ; i < n; ++i) {
    Modifier* cur_mod = _mods[i];
    if (cur_mod && ! cur_mod->check(request_data)) return false;
  }

  return true;
}

enum mod_errors {
  ME_UNKNOWN,
  ME_PARSE_FAILED,
  ME_BAD_MOD,
  ME_CALLEE_GENERATED
};

static const char *errorFormats[] = {
  "Unknown error parsing modifier",
  "Unable to parse modifier",
  "Unknown modifier",
  "Callee Generated",
};

ControlBase::Modifier*
ControlBase::findModOfType(Modifier::Type t) const {
  for (int i = 0, n = _mods.size(); i < n; ++i) {
      Modifier* m = _mods[i];
      if (m && t == m->type()) return m;
  }
  return 0;
}

const char *
ControlBase::ProcessModifiers(matcher_line * line_info) {
  // Variables for error processing
  const char *errBuf = NULL;
  mod_errors err = ME_UNKNOWN;

  int n_elts = line_info->num_el; // Element count for line.

  // No elements -> no modifiers.
  if (0 >= n_elts) return 0;
  // Can't have more modifiers than elements, so reasonable upper bound.
  _mods.clear();
  _mods.reserve(n_elts);

  // As elements are consumed, the labels are nulled out and the element
  // count decremented. So we have to scan the entire array to be sure of
  // finding all the elements. We'll track the element count so we can
  // escape if we've found all of the elements.
  for (int i = 0; n_elts && ME_UNKNOWN == err && i < MATCHER_MAX_TOKENS; ++i) {
    Modifier* mod = 0;

    char * label = line_info->line[0][i];
    char * value = line_info->line[1][i];

    if (!label) continue; // Already use.
    if (!value) {
      err = ME_PARSE_FAILED;
      break;
    }

    if (strcasecmp(label, "port") == 0) {
      mod = PortMod::make(value, &errBuf);
    } else if (strcasecmp(label, "iport") == 0) {
      mod = IPortMod::make(value, &errBuf);
    } else if (strcasecmp(label, "scheme") == 0) {
      mod = SchemeMod::make(value, &errBuf);
    } else if (strcasecmp(label, "method") == 0) {
      mod = MethodMod::make(value, &errBuf);
    } else if (strcasecmp(label, "prefix") == 0) {
      mod = PrefixMod::make(value, &errBuf);
    } else if (strcasecmp(label, "suffix") == 0) {
      mod = SuffixMod::make(value, &errBuf);
    } else if (strcasecmp(label, "src_ip") == 0) {
      mod = SrcIPMod::make(value, &errBuf);
    } else if (strcasecmp(label, "time") == 0) {
      mod = TimeMod::make(value, &errBuf);
    } else if (strcasecmp(label, "tag") == 0) {
      mod = TagMod::make(value, &errBuf);
    } else {
      err = ME_BAD_MOD;
    }

    if (errBuf) err = ME_CALLEE_GENERATED; // Mod make failed.

    // If nothing went wrong, add the mod and bump the element count.
    if (ME_UNKNOWN == err) {
      _mods.push_back(mod);
      --n_elts;
    }
  }

  if (err != ME_UNKNOWN) {
    this->clear();
    if (err != ME_CALLEE_GENERATED) {
      errBuf = errorFormats[err];
    }
  }

  return errBuf;
}
