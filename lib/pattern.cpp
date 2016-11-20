/******************************************************************************\
* Copyright (c) 2016, Robert van Engelen, Genivia Inc. All rights reserved.    *
*                                                                              *
* Redistribution and use in source and binary forms, with or without           *
* modification, are permitted provided that the following conditions are met:  *
*                                                                              *
*   (1) Redistributions of source code must retain the above copyright notice, *
*       this list of conditions and the following disclaimer.                  *
*                                                                              *
*   (2) Redistributions in binary form must reproduce the above copyright      *
*       notice, this list of conditions and the following disclaimer in the    *
*       documentation and/or other materials provided with the distribution.   *
*                                                                              *
*   (3) The name of the author may not be used to endorse or promote products  *
*       derived from this software without specific prior written permission.  *
*                                                                              *
* THIS SOFTWARE IS PROVIDED BY THE AUTHOR ``AS IS'' AND ANY EXPRESS OR IMPLIED *
* WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES OF         *
* MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO   *
* EVENT SHALL THE AUTHOR BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,       *
* SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, *
* PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS;  *
* OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY,     *
* WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR      *
* OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF       *
* ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.                                   *
\******************************************************************************/

/**
@file      pattern.cpp
@brief     RE/Flex regular expression pattern compiler
@author    Robert van Engelen - engelen@genivia.com
@copyright (c) 2015-2016, Robert van Engelen, Genivia Inc. All rights reserved.
@copyright (c) BSD-3 License - see LICENSE.txt
*/

#include "pattern.h"
#include <cstdlib>

#ifdef TIMED
// TODO consider adding a profiling API to obtain stats, just as nodes() and edges()
#include <time.h>
#include <sys/times.h>
#include <unistd.h>
static long clock_ratio = 1000/sysconf(_SC_CLK_TCK);
static struct tms clock_buf;
static clock_t clock_time;
#define start_clock (clock_time = times(&clock_buf))
#define stop_clock (clock_time = times(&clock_buf) - clock_time)
#define show_timer(s, t) fprintf(stderr, "%s elapsed real time = %lu (ms)\n", s, clock_ratio*t)
#define show_clock(s) show_timer(s, clock_time)
#endif

#ifdef DEBUG
# define DBGLOGPOS(p) \
  if ((p).accept()) \
  { \
    DBGLOGA(" (%hu)", (p).accepts()); \
    if ((p).lazy()) \
      DBGLOGA("?%zu", (p).lazy()); \
    if ((p).greedy()) \
      DBGLOGA("!"); \
  } \
  else \
  { \
    DBGLOGA(" "); \
    if ((p).iter()) \
      DBGLOGA("%hu.", (p).iter()); \
    DBGLOGA("%lu", (p).loc()); \
    if ((p).lazy()) \
      DBGLOGA("?%zu", (p).lazy()); \
    if ((p).anchor()) \
      DBGLOGA("^"); \
    if ((p).greedy()) \
      DBGLOGA("!"); \
    if ((p).ticked()) \
      DBGLOGA("'"); \
  }
#endif

namespace reflex {

static const char *posix_class[] = {
  "ASCII",
  "Space",
  "Xdigit",
  "Cntrl",
  "Print",
  "Alnum",
  "Alpha",
  "Blank",
  "Digit",
  "Graph",
  "Lower",
  "Punct",
  "Upper",
  "Word",
};

static const char *meta_label[] = {
  NULL,
  "NWB",
  "NWE",
  "BWB",
  "EWB",
  "BWE",
  "EWE",
  "BOL",
  "EOL",
  "BOB",
  "EOB",
  "IND",
  "DED",
};

const std::string Pattern::operator[](Index choice) const
{
  if (choice == 0)
    return rex_;
  if (choice >= 1 && choice <= size())
  {
    Location loc = end_.at(choice - 1);
    Location prev = 0;
    if (choice >= 2)
      prev = end_.at(choice - 2) + 1;
    return rex_.substr(prev, loc - prev);
  }
  return "";
}

void Pattern::error(enum Error::Code code, const char *message, size_t loc) const throw (Error)
{
  Error e(this, code, message, loc);
  if (opt_.w)
    e.display();
  if (opt_.r || code == Error::CODE_OVERFLOW)
    throw e;
}

void Pattern::init(const char *opt) throw (Error)
{
  if (opc_)
  {
    nop_ = 0;
  }
  else
  {
    Positions startpos;
    Follow    followpos;
    Map       modifiers;
    Map       lookahead;
    init_options(opt);
#ifdef TIMED
    start_clock;
#endif
    parse(startpos, followpos, modifiers, lookahead);
#ifdef TIMED
    stop_clock;
    show_clock("parse");
#endif
    State start(startpos);
    compile(start, followpos, modifiers, lookahead);
#ifdef TIMED
    start_clock;
#endif
    assemble(start);
#ifdef TIMED
    stop_clock;
    show_clock("assemble");
#endif
  }
}

void Pattern::init_options(const char *opt)
{
  opt_.b = false;
  opt_.i = false;
  opt_.l = false;
  opt_.m = false;
  opt_.q = false;
  opt_.r = false;
  opt_.s = false;
  opt_.w = false;
  opt_.x = false;
  opt_.e = '\\';
  if (opt)
  {
    for (const char *s = opt; *s != '\0'; ++s)
    {
      switch (*s)
      {
        case 'b':
          opt_.b = true;
          break;
        case 'e':
          opt_.e = (*(s += (s[1] == '=') + 1) == ';' ? '\0' : *s);
          break;
        case 'i':
          opt_.i = true;
          break;
        case 'l':
          opt_.l = true;
          break;
        case 'm':
          opt_.m = true;
          break;
        case 'q':
          opt_.q = true;
          break;
        case 'r':
          opt_.r = true;
          break;
        case 's':
          opt_.s = true;
          break;
        case 'w':
          opt_.w = true;
          break;
        case 'x':
          opt_.x = true;
          break;
        case 'f':
        case 'n':
          for (const char *t = s += (s[1] == '='); *s != ';' && *s != '\0'; ++t)
          {
            if (*t == ',' || std::isspace(*t) || *t == ';' || *t == '\0')
            {
              if (t > s + 1)
              {
                std::string name(s + 1, t - s - 1);
                if (name.find('.') == std::string::npos)
                  opt_.n = name;
                else
                  opt_.f.push_back(name);
              }
              s = t;
            }
          }
          --s;
          break;
      }
    }
  }
}

void Pattern::parse(
    Positions& startpos,
    Follow&    followpos,
    Map&       modifiers,
    Map&       lookahead) throw (Error)
{
  DBGLOG("BEGIN parse()");
  Location  loc = 0;
  Index     choice = 1;
  Positions firstpos;
  Positions lastpos;
  bool      nullable;
  Index     iter;
  do
  {
    Positions lazypos;
    parse2(
        true,
        loc,
        firstpos,
        lastpos,
        nullable,
        followpos,
        lazypos,
        modifiers,
        lookahead[choice],
        iter);
    end_.push_back(loc);
    set_insert(startpos, firstpos);
    if (nullable)
    {
      if (lazypos.empty())
      {
        startpos.insert(Position(choice).accept(true));
      }
      else
      {
        for (Positions::const_iterator p = lazypos.begin(); p != lazypos.end(); ++p)
          startpos.insert(Position(choice).accept(true).lazy(p->loc()));
      }
    }
    for (Positions::const_iterator p = lastpos.begin(); p != lastpos.end(); ++p)
    {
      if (lazypos.empty())
      {
        followpos[p->pos()].insert(Position(choice).accept(true));
      }
      else
      {
        for (Positions::const_iterator q = lazypos.begin(); q != lazypos.end(); ++q)
          followpos[p->pos()].insert(Position(choice).accept(true).lazy(q->loc()));
      }
    }
    ++choice;
  } while (at(loc++) == '|');
#ifdef DEBUG
  DBGLOGN("startpos = {");
  for (Positions::const_iterator p = startpos.begin(); p != startpos.end(); ++p)
    DBGLOGPOS(*p);
  DBGLOGA(" }");
  for (Follow::const_iterator fp = followpos.begin(); fp != followpos.end(); ++fp)
  {
    DBGLOGN("followpos(");
    DBGLOGPOS(fp->first);
    DBGLOGA(" ) = {");
    for (Positions::const_iterator p = fp->second.begin(); p != fp->second.end(); ++p)
      DBGLOGPOS(*p);
    DBGLOGA(" }");
  }
#endif
  DBGLOG("END parse()");
}

void Pattern::parse1(
    bool       begin,
    Location&  loc,
    Positions& firstpos,
    Positions& lastpos,
    bool&      nullable,
    Follow&    followpos,
    Positions& lazypos,
    Map&       modifiers,
    Ranges&    lookahead,
    Index&     iter) throw (Error)
{
  DBGLOG("BEGIN parse1(%zu)", loc);
  parse2(
      begin,
      loc,
      firstpos,
      lastpos,
      nullable,
      followpos,
      lazypos,
      modifiers,
      lookahead,
      iter);
  Positions firstpos1;
  Positions lastpos1;
  bool      nullable1;
  Positions lazypos1;
  Index     iter1;
  while (at(loc) == '|')
  {
    ++loc;
    parse2(
        begin,
        loc,
        firstpos1,
        lastpos1,
        nullable1,
        followpos,
        lazypos1,
        modifiers,
        lookahead,
        iter1);
    set_insert(firstpos, firstpos1);
    set_insert(lastpos, lastpos1);
    set_insert(lazypos, lazypos1);
    if (nullable1)
      nullable = true;
    if (iter1 > iter)
      iter = iter1;
  }
  DBGLOG("END parse1()");
}

void Pattern::parse2(
    bool       begin,
    Location&  loc,
    Positions& firstpos,
    Positions& lastpos,
    bool&      nullable,
    Follow&    followpos,
    Positions& lazypos,
    Map&       modifiers,
    Ranges&    lookahead,
    Index&     iter) throw (Error)
{
  DBGLOG("BEGIN parse2(%zu)", loc);
  Positions a_pos;
  if (begin)
  {
    while (true)
    {
      if (opt_.x)
        while (std::isspace(at(loc)))
          ++loc;
      if (at(loc) == '^')
      {
        a_pos.insert(Position(loc++));
        // begin = false; // FIXME 7/29 but does not allow ^ as a pattern
      }
      else if (escapes_at(loc, "ABb<>"))
      {
        a_pos.insert(Position(loc));
        loc += 2;
        // begin = false; // FIXME 7/29 but does not allow \b as a pattern
      }
      else
      {
        if (escapes_at(loc, "ij"))
          begin = false;
        break;
      }
    }
  }
  parse3(
      begin,
      loc,
      firstpos,
      lastpos,
      nullable,
      followpos,
      lazypos,
      modifiers,
      lookahead,
      iter);
  Positions firstpos1;
  Positions lastpos1;
  bool      nullable1;
  Positions lazypos1;
  Index     iter1;
  Position  l_pos;
  Char      c;
  while ((c = at(loc)) != '\0' && c != '|' && c != ')')
  {
    if (c == '/' && l_pos == Position::NPOS && opt_.l && (!opt_.x || at(loc + 1) != '*'))
      l_pos = loc++; // lookahead
    parse3(
        false,
        loc,
        firstpos1,
        lastpos1,
        nullable1,
        followpos,
        lazypos1,
        modifiers,
        lookahead,
        iter1);
    if (c == '/' && l_pos != Position::NPOS)
      firstpos1.insert(l_pos);
    if (!lazypos.empty()) // TODO this is an extra rule for + only and (may) not be needed for *
    {
      // FIXME lazy(lazypos, firstpos1); does not work for (a|b)*?a*b+, below works
      Positions firstpos2;
      lazy(lazypos, firstpos1, firstpos2);
      set_insert(firstpos1, firstpos2);
      // if (lazypos1.empty())
        // greedy(firstpos1); // FIXME 8/1 works except fails for ((a|b)*?b){2} and (a|b)??(a|b)??aa
    }
    if (nullable)
      set_insert(firstpos, firstpos1);
    for (Positions::const_iterator p = lastpos.begin(); p != lastpos.end(); ++p)
      set_insert(followpos[p->pos()], firstpos1);
    if (nullable1)
    {
      set_insert(lastpos, lastpos1);
    }
    else
    {
      lastpos.swap(lastpos1);
      nullable = false;
    }
    set_insert(lazypos, lazypos1);
    if (iter1 > iter)
      iter = iter1;
  }
  for (Positions::const_iterator p = a_pos.begin(); p != a_pos.end(); ++p)
  {
    for (Positions::const_iterator k = lastpos.begin(); k != lastpos.end(); ++k)
      if ((at(k->loc()) == ')' || (opt_.l && at(k->loc()) == '/')) && lookahead.find(k->loc()) != lookahead.end())
        followpos[p->pos()].insert(*k);
    for (Positions::const_iterator k = lastpos.begin(); k != lastpos.end(); ++k)
      followpos[k->pos()].insert(p->anchor(!nullable || k->pos() != p->pos()));
    lastpos.clear();
    lastpos.insert(*p);
    if (nullable)
    {
      firstpos.insert(*p);
      nullable = false;
    }
  }
  if (l_pos != Position::NPOS)
  {
    for (Positions::const_iterator p = lastpos.begin(); p != lastpos.end(); ++p)
      followpos[p->pos()].insert(l_pos.ticked(true)); // ticked for lookstop
    lastpos.insert(l_pos.ticked(true));
    lookahead.insert(l_pos.loc(), l_pos.loc());
  }
  DBGLOG("END parse2()");
}

void Pattern::parse3(
    bool       begin,
    Location&  loc,
    Positions& firstpos,
    Positions& lastpos,
    bool&      nullable,
    Follow&    followpos,
    Positions& lazypos,
    Map&       modifiers,
    Ranges&    lookahead,
    Index&     iter) throw (Error)
{
  DBGLOG("BEGIN parse3(%zu)", loc);
  Position b_pos(loc);
  parse4(
      begin,
      loc,
      firstpos,
      lastpos,
      nullable,
      followpos,
      lazypos,
      modifiers,
      lookahead,
      iter);
  Char c = at(loc);
  if (opt_.x)
    while (std::isspace(c))
      c = at(++loc);
#if 0 // FIXME 7/31 for testing only, use ! to force greedy
  if (c == '!')
  {
    ++loc;
    greedy(firstpos);
  }
  else
#endif
  if (c == '*' || c == '+' || c == '?')
  {
    if (c == '*' || c == '?')
      nullable = true;
    if (at(++loc) == '?')
    {
      lazypos.insert(loc);
      if (nullable)
        lazy(lazypos, firstpos);
      ++loc;
    }
    else
    {
      // FIXME 7/30 if (!nullable)
        // FIXME 7/30 lazypos.clear();
      greedy(firstpos);
    }
    if (c == '+' && !nullable && !lazypos.empty())
    {
      Positions firstpos1;
      lazy(lazypos, firstpos, firstpos1);
      for (Positions::const_iterator p = lastpos.begin(); p != lastpos.end(); ++p)
        set_insert(followpos[p->pos()], firstpos1);
      set_insert(firstpos, firstpos1);
    }
    else if (c == '*' || c == '+')
    {
      for (Positions::const_iterator p = lastpos.begin(); p != lastpos.end(); ++p)
        set_insert(followpos[p->pos()], firstpos);
    }
  }
  else if (c == '{') // {n,m} repeat min n times to max m
  {
    size_t k = 0;
    for (Location i = 0; i < 7 && std::isdigit(c = at(++loc)); ++i)
      k = 10 * k + (c - '0');
    if (k > IMAX)
      error(Error::REGEX_RANGE, "{min,max} range overflow", loc);
    Index n = static_cast<Index>(k);
    Index m = n;
    bool unlimited = false;
    if (at(loc) == ',')
    {
      if (std::isdigit(at(loc + 1)))
      {
        m = 0;
        for (Location i = 0; i < 7 && std::isdigit(c = at(++loc)); ++i)
          m = 10 * m + (c - '0');
      }
      else
      {
        unlimited = true;
        ++loc;
      }
    }
    if (at(loc) == '}')
    {
      bool nullable1 = nullable;
      if (n == 0)
        nullable = true;
      if (n > m)
        error(Error::REGEX_RANGE, "min > max in range {min,max}", loc);
      if (at(++loc) == '?')
      {
        lazypos.insert(loc);
        if (nullable)
        {
          lazy(lazypos, firstpos);
        }
        /* FIXME 8/1 else
        {
          lazy(lazypos, firstpos, firstpos1);
          set_insert(firstpos, firstpos1);
          pfirstpos = &firstpos1;
        } */
        ++loc;
      }
      else
      {
        // FIXME 7/30 if (!nullable)
          // FIXME 7/30 lazypos.clear();
        if (n < m && lazypos.empty())
          greedy(firstpos);
      }
      // FIXME added pfirstpos to point to updated firstpos with lazy quants
      Positions firstpos1, *pfirstpos = &firstpos;
      if (!nullable && !lazypos.empty()) // FIXME 8/1 added to make ((a|b)*?b){2} work
      {
        lazy(lazypos, firstpos, firstpos1);
        pfirstpos = &firstpos1;
      }
      if (nullable && unlimited) // {0,} == *
      {
        for (Positions::const_iterator p = lastpos.begin(); p != lastpos.end(); ++p)
          set_insert(followpos[p->pos()], *pfirstpos);
      }
      else if (m > 0)
      {
        if (iter * m >= IMAX)
          error(Error::REGEX_RANGE, "{min,max} range overflow", loc);
        // update followpos by virtually replicating sub-regex m-1 times
        Follow followpos1;
        for (Follow::const_iterator fp = followpos.begin(); fp != followpos.end(); ++fp)
          if (fp->first >= b_pos)
            for (Index i = 1; i < m; ++i)
              for (Positions::const_iterator p = fp->second.begin(); p != fp->second.end(); ++p)
                followpos1[fp->first.iter(iter * i)].insert(p->iter(iter * i));
        for (Follow::const_iterator fp = followpos1.begin(); fp != followpos1.end(); ++fp)
          set_insert(followpos[fp->first], fp->second);
        // add m-1 times virtual concatenation (by indexed positions k.i)
        for (Index i = 0; i < m - 1; ++i)
          for (Positions::const_iterator k = lastpos.begin(); k != lastpos.end(); ++k)
            for (Positions::const_iterator j = pfirstpos->begin(); j != pfirstpos->end(); ++j)
              followpos[k->pos().iter(iter * i)].insert(j->iter(iter * i + iter));
        if (unlimited)
          for (Positions::const_iterator k = lastpos.begin(); k != lastpos.end(); ++k)
            for (Positions::const_iterator j = pfirstpos->begin(); j != pfirstpos->end(); ++j)
              followpos[k->pos().iter(iter * m - iter)].insert(j->iter(iter * m - iter));
        if (nullable1)
        {
          // extend firstpos when sub-regex is nullable
          Positions firstpos1 = *pfirstpos;
          for (Index i = 1; i <= m - 1; ++i)
            for (Positions::const_iterator k = firstpos1.begin(); k != firstpos1.end(); ++k)
              firstpos.insert(k->iter(iter * i));
        }
        // n to m-1 are optional with all 0 to m-1 are optional when nullable
        Positions lastpos1;
        for (Index i = (nullable ? 0 : n - 1); i <= m - 1; ++i)
          for (Positions::const_iterator k = lastpos.begin(); k != lastpos.end(); ++k)
            lastpos1.insert(k->iter(iter * i));
        lastpos.swap(lastpos1);
        iter *= m;
      }
      else // zero range {0}
      {
        firstpos.clear();
        lastpos.clear();
        lazypos.clear();
      }
    }
    else
    {
      error(Error::REGEX_SYNTAX, "malformed range {min,max}", loc);
    }
  }
  else if (c == '}')
  {
    error(Error::REGEX_SYNTAX, "missing {", loc++);
  }
  DBGLOG("END parse3()");
}

void Pattern::parse4(
    bool       begin,
    Location&  loc,
    Positions& firstpos,
    Positions& lastpos,
    bool&      nullable,
    Follow&    followpos,
    Positions& lazypos,
    Map&       modifiers,
    Ranges&    lookahead,
    Index&     iter) throw (Error)
{
  DBGLOG("BEGIN parse4(%zu)", loc);
  firstpos.clear();
  lastpos.clear();
  nullable = true;
  lazypos.clear();
  iter = 1;
  Char c = at(loc);
  if (c == '(')
  {
    if (at(++loc) == '?')
    {
      c = at(++loc);
      if (c == '#') // (?# comment
      {
        while ((c = at(++loc)) != '\0' && c != ')')
          continue;
        if (c == ')')
          ++loc;
      }
      else if (c == '^') // (?^ negative pattern to be ignored (new mode)
      {
        ++loc;
        parse1(
            begin,
            loc,
            firstpos,
            lastpos,
            nullable,
            followpos,
            lazypos,
            modifiers,
            lookahead,
            iter);
        for (Positions::const_iterator p = lastpos.begin(); p != lastpos.end(); ++p)
          followpos[p->pos()].insert(Position(0).accept(true));
      }
      else if (c == '=') // (?= lookahead
      {
        Position l_pos(loc++ - 2); // lookahead at (
        parse1(
            begin,
            loc,
            firstpos,
            lastpos,
            nullable,
            followpos,
            lazypos,
            modifiers,
            lookahead,
            iter);
        firstpos.insert(l_pos);
        if (nullable)
          lastpos.insert(l_pos);
        if (lookahead.find(l_pos.loc(), loc) == lookahead.end()) // do not permit nested lookaheads
          lookahead.insert(l_pos.loc(), loc); // lookstop at )
        for (Positions::const_iterator p = lastpos.begin(); p != lastpos.end(); ++p)
          followpos[p->pos()].insert(Position(loc).ticked(true));
        lastpos.insert(Position(loc).ticked(true));
        if (nullable)
        {
          firstpos.insert(Position(loc).ticked(true));
          lastpos.insert(l_pos);
        }
      }
      else if (c == ':')
      {
        ++loc;
        parse1(
            begin,
            loc,
            firstpos,
            lastpos,
            nullable,
            followpos,
            lazypos,
            modifiers,
            lookahead,
            iter);
      }
      else
      {
        Location m_loc = loc;
        bool opt_i = opt_.i;
        bool opt_q = opt_.q;
        bool opt_m = opt_.m;
        bool opt_s = opt_.s;
        bool opt_x = opt_.x;
        do
        {
          if (c == 'i')
            opt_.i = true;
          else if (c == 'l')
            opt_.l = true;
          else if (c == 'm')
            opt_.m = true;
          else if (c == 'q')
            opt_.q = true;
          else if (c == 's')
            opt_.s = true;
          else if (c == 'x')
            opt_.x = true;
          else
            error(Error::REGEX_SYNTAX, "unrecognized modifier", loc);
          c = at(++loc);
        } while (c != '\0' && c != ':' && c != ')');
        if (c != '\0')
          ++loc;
        if (m_loc == 2 && c == ')') // enforce (?imqsx)global options
        {
          parse2(
              begin,
              loc,
              firstpos,
              lastpos,
              nullable,
              followpos,
              lazypos,
              modifiers,
              lookahead,
              iter);
        }
        else
        {
          parse1(
              begin,
              loc,
              firstpos,
              lastpos,
              nullable,
              followpos,
              lazypos,
              modifiers,
              lookahead,
              iter);
          do
          {
            c = at(m_loc++);
            if (c != '\0' && c != 'q' && c != 'x' && c != ':' && c != ')')
              modifiers[c].insert(m_loc, loc);
          } while (c != '\0' && c != ':' && c != ')');
          opt_.i = opt_i;
          opt_.q = opt_q;
          opt_.m = opt_m;
          opt_.s = opt_s;
          opt_.x = opt_x;
        }
      }
    }
    else
    {
      parse1(
          begin,
          loc,
          firstpos,
          lastpos,
          nullable,
          followpos,
          lazypos,
          modifiers,
          lookahead,
          iter);
    }
    if (c != ')')
    {
      if (at(loc) == ')')
        ++loc;
      else
        error(Error::REGEX_SYNTAX, "missing )", loc);
    }
  }
  else if (c == '[')
  {
    firstpos.insert(loc);
    lastpos.insert(loc);
    nullable = false;
    if ((c = at(++loc)) == '^')
      c = at(++loc);
    while (c != '\0')
    {
      if (c == '[' && at(loc + 1) == ':')
      {
        Location c_loc = find_at(loc + 2, ':');
        if (c_loc != std::string::npos && at(c_loc + 1) == ']')
          loc = c_loc + 1;
      }
      if ((c = at(++loc)) == ']')
      {
        ++loc;
        break;
      }
    }
    if (c == '\0')
      error(Error::REGEX_SYNTAX, "missing ]", loc);
  }
  else if ((c == '"' && opt_.q) || escape_at(loc) == 'Q')
  {
    bool quoted = (c == '"');
    if (!quoted)
      ++loc;
    Location q_loc = loc++;
    if ((c = at(loc)) != '\0' && (!quoted || c != '"') && (quoted || c != opt_.e || at(loc + 1) != 'E'))
    {
      firstpos.insert(loc);
      Position p;
      do
      {
        if (c == '\\' && at(loc + 1) == '"' && quoted)
          ++loc;
        if (p != Position::NPOS)
          followpos[p].insert(loc);
        p = loc++;
      } while ((c = at(loc)) != '\0' && (!quoted || c != '"') && (quoted || c != opt_.e || at(loc + 1) != 'E'));
      lastpos.insert(p);
      nullable = false;
    }
    modifiers['q'].insert(q_loc, loc);
    if (c != '\0')
    {
      if (!quoted)
        ++loc;
      if (at(loc) != '\0')
        ++loc;
    }
    else
    {
      error(Error::REGEX_SYNTAX, quoted ? "missing \"" : "missing \\E", loc);
    }
  }
  else if (c == '#' && opt_.x)
  {
    ++loc;
    while ((c = at(loc)) != '\0' && c != '\n')
      ++loc;
    if (c == '\n')
      ++loc;
  }
  else if (c == '/' && opt_.l && opt_.x && at(loc + 1) == '*')
  {
    loc += 2;
    while ((c = at(loc)) != '\0' && (c != '*' || at(loc + 1) != '/'))
      ++loc;
    if (c != '\0')
      loc += 2;
    else
      error(Error::REGEX_SYNTAX, "missing */", loc);
  }
  else if (std::isspace(c) && opt_.x)
  {
    ++loc;
  }
  else if (c != '\0' && c != '|' && c != ')' && c != '?' && c != '*' && c != '+')
  {
    if (begin && (c == '$' || escapes_at(loc, "AZBb<>ij") != '\0'))
      error(Error::REGEX_SYNTAX, "empty pattern", loc + 1);
    firstpos.insert(loc);
    lastpos.insert(loc);
    nullable = false;
    parse_esc(loc);
  }
  else if (!begin || c != '\0') // permits empty regex pattern but not empty subpatterns
  {
    error(Error::REGEX_SYNTAX, "empty pattern", loc);
  }
  DBGLOG("END parse4()");
}

void Pattern::parse_esc(Location& loc) const throw (Error)
{
  Char c;
  if (at(loc++) == opt_.e && opt_.e != '\0' && (c = at(loc)) != '\0')
  {
    if (c == '0')
    {
      ++loc;
      for (int i = 0; i < 3 && std::isdigit(at(loc)); ++i)
        ++loc;
    }
    else if (c == 'p' && at(loc + 1) == '{')
    {
      ++loc;
      while (std::isalnum(at(++loc)))
        continue;
      if (at(loc) == '}')
        ++loc;
      else
        error(Error::REGEX_SYNTAX, "malformed \\p{}", loc);
    }
    else if (c == 'u' && at(loc + 1) == '{')
    {
      ++loc;
      while (std::isxdigit(at(++loc)))
        continue;
      if (at(loc) == '}')
        ++loc;
      else
        error(Error::REGEX_SYNTAX, "malformed \\u{}", loc);
    }
    else if (c == 'x' && at(loc + 1) == '{')
    {
      ++loc;
      while (std::isxdigit(at(++loc)))
        continue;
      if (at(loc) == '}')
        ++loc;
      else
        error(Error::REGEX_SYNTAX, "malformed \\x{}", loc);
    }
    else if (c == 'x')
    {
      ++loc;
      for (int i = 0; i < 2 && std::isxdigit(at(loc)); ++i)
        ++loc;
    }
    else
    {
      if (c == 'c')
        ++loc;
      if (at(loc) != '\0')
        ++loc;
      else
        error(Error::REGEX_SYNTAX, "malformed \\c", loc);
    }
  }
}

void Pattern::compile(
    State&     start,
    Follow&    followpos,
    const Map& modifiers,
    const Map& lookahead) throw (Error)
{
  DBGLOG("BEGIN compile()");
  State *back_state = &start;
#ifdef TIMED
  clock_t timer1 = 0;
  clock_t timer2 = 0;
#endif
  vno_ = 0;
  eno_ = 0;
  acc_.resize(end_.size(), false);
  trim_lazy(start);
  for (State *state = &start; state; state = state->next)
  {
    Moves moves;
#ifdef TIMED
    start_clock;
#endif
    compile_transition(
        state,
        followpos,
        modifiers,
        lookahead,
        moves);
#ifdef TIMED
    timer1 += stop_clock;
    start_clock;
#endif
    for (Moves::iterator i = moves.begin(); i != moves.end(); ++i)
    {
      Positions& pos = i->second;
      trim_lazy(pos);
      if (!pos.empty())
      {
        State *target_state = &start;
        State **branch_ptr = NULL;
        // binary search for a matching state
        do
        {
          if (pos < *target_state)
            target_state = *(branch_ptr = &target_state->left);
          else if (pos > *target_state)
            target_state = *(branch_ptr = &target_state->right);
          else
            break;
        } while (target_state);
        if (!target_state)
          back_state = back_state->next = *branch_ptr = target_state = new State(pos);
#ifdef BITS
        size_t lo = i->first.find_first(), j = lo, k = lo;
        for (;;)
        {
          if (j != k)
          {
            state->edges[lo] = std::pair<Char,State*>(k - 1, target_state);
            lo = k = j;
          }
          if (j == Bits::npos)
            break;
          j = i->first.find_next(j);
          ++k;
          ++eno_;
        }
#else
        for (Chars::const_iterator j = i->first.begin(); j != i->first.end(); ++j)
        {
          state->edges[j->first] = std::pair<Char,State*>(j->second - 1, target_state); // -1 to adjust open ended [lo,hi)
          eno_ += j->second - j->first;
        }
#endif
      }
    }
    if (state->accept > 0 && state->accept <= end_.size())
      acc_[state->accept - 1] = true;
#ifdef TIMED
    timer2 += stop_clock;
#endif
    ++vno_;
  }
#ifdef TIMED
  show_timer("compile moves", timer1);
  show_timer("compile edges", timer2);
#endif
  DBGLOG("END compile()");
}

void Pattern::lazy(
    const Positions& lazypos,
    Positions&       pos) const
{
  if (!lazypos.empty())
  {
    Positions pos1;
    lazy(lazypos, pos, pos1);
    pos.swap(pos1);
  }
}

void Pattern::lazy(
    const Positions& lazypos,
    const Positions& pos,
    Positions&       pos1) const
{
  for (Positions::const_iterator p = pos.begin(); p != pos.end(); ++p)
    for (Positions::const_iterator q = lazypos.begin(); q != lazypos.end(); ++q)
      // pos1.insert(p->lazy() ? *p : p->lazy(q->loc())); // FIXME only if p is not already lazy??
      pos1.insert(p->lazy(q->loc())); // overrides lazyness even when p is already lazy
}

void Pattern::greedy(Positions& pos) const
{
  Positions pos1;
  for (Positions::const_iterator p = pos.begin(); p != pos.end(); ++p)
    // pos1.insert(p->lazy() ? *p : p->greedy(true)); // FIXME 7/29 guard added: p->lazy() ? *p : p->greedy(true)
    pos1.insert(p->lazy(0).greedy(true));
  pos.swap(pos1);
}

void Pattern::trim_lazy(Positions& pos) const
{
  Positions::reverse_iterator p = pos.rbegin();
  while (p != pos.rend() && p->lazy())
  {
    Location l = p->lazy();
    if (p->accept() || p->anchor()) // FIXME 7/28 added p->anchor()
    {
      pos.insert(p->lazy(0)); // make lazy accept/anchor a non-lazy accept/anchor
      pos.erase(--p.base());
      while (p != pos.rend() && p->lazy() == l)
#if 0 // FIXME set to 1 to turn lazy trimming off
        ++p;
#else
        pos.erase(--p.base());
#endif
    }
    else
    {
#if 0 // FIXME 7/31
      if (p->greedy())
      {
        pos.insert(p->lazy(0).greedy(false));
        pos.erase(--p.base());
      }
      else
      {
        break; // ++p;
      }
#else
      if (!p->greedy()) // stop here, greedy bit is 0 from here on
        break;
      pos.insert(p->lazy(0));
      ++p;
#endif
    }
  }
#if 0 // FIXME 7/31 but results in more states
  while (p != pos.rend() && p->greedy())
  {
    pos.insert(p->greedy(false));
    pos.erase(--p.base());
  }
#endif
}

void Pattern::compile_transition(
    State     *state,
    Follow&    followpos,
    const Map& modifiers,
    const Map& lookahead,
    Moves&     moves) const throw (Error)
{
  DBGLOG("BEGIN compile_transition()");
  Positions::const_iterator end = state->end();
  for (Positions::const_iterator k = state->begin(); k != end; ++k)
  {
    if (k->accept())
    {
      Index accept = k->accepts();
      if (state->accept == 0 || accept < state->accept)
        state->accept = accept; // pick lowest nonzero accept index
      if (!accept)
        state->redo = true;
    }
    else
    {
      Location loc = k->loc();
      Char c = at(loc);
      DBGLOGN("At %lu: %c", loc, c);
      bool literal = is_modified('q', modifiers, loc);
      if (c == '/' && opt_.l && !literal)
      {
        Position n(0);
        DBGLOG("LOOKAHEAD");
        for (Map::const_iterator i = lookahead.begin(); i != lookahead.end(); ++i)
        {
          Ranges::const_iterator j = i->second.find(loc);
          DBGLOGN("%d %d (%d) %lu", state->accept, i->first, j != i->second.end(), n.loc());
          if (j != i->second.end())
          {
            if (!k->ticked())
              state->heads.insert(static_cast<Index>(n + std::distance(i->second.begin(), j)));
            else // FIXME 7/18 if (state->accept == i->first) no longer check for accept state, assume we are at an accept state
              state->tails.insert(static_cast<Index>(n + std::distance(i->second.begin(), j)));
          }
          n = n + i->second.size();
        }
      }
      else if (c == '(' && !literal)
      {
        Position n(0);
        DBGLOG("LOOKAHEAD HEAD");
        for (Map::const_iterator i = lookahead.begin(); i != lookahead.end(); ++i)
        {
          Ranges::const_iterator j = i->second.find(loc);
          DBGLOGN("%d %d (%d) %lu", state->accept, i->first, j != i->second.end(), n.loc());
          if (j != i->second.end())
            state->heads.insert(static_cast<Index>(n + std::distance(i->second.begin(), j)));
          n = n + i->second.size();
        }
      }
      else if (c == ')' && !literal)
      {
        /* FIXME 7/18 do no longer check for accept state, assume we are at an accept state
        if (state->accept > 0)
        */
        {
          Position n(0);
          DBGLOG("LOOKAHEAD TAIL");
          for (Map::const_iterator i = lookahead.begin(); i != lookahead.end(); ++i)
          {
            Ranges::const_iterator j = i->second.find(loc);
            DBGLOGN("%d %d (%d) %lu", state->accept, i->first, j != i->second.end(), n.loc());
            if (j != i->second.end() /* FIXME 7/18 && state->accept == i->first */ ) // only add lookstop when part of the proper accept state
              state->tails.insert(static_cast<Index>(n + std::distance(i->second.begin(), j)));
            n = n + i->second.size();
          }
        }
      }
      else
      {
        Follow::const_iterator i = followpos.find(k->pos());
        if (i != followpos.end())
        {
          if (k->lazy())
          {
#if 1 // FIXME 7/31 this optimization works fine when trim_lazy adds non-lazy greedy state, but may increase the total number of states:
            if (k->greedy())
              continue;
#endif
            Follow::iterator j = followpos.find(*k);
            if (j == followpos.end())
            {
              // followpos is not defined for lazy pos yet, so add lazy followpos (memoization)
              j = followpos.insert(std::pair<Position,Positions>(*k, Positions())).first;
              for (Positions::const_iterator p = i->second.begin(); p != i->second.end(); ++p)
                j->second.insert(/* p->lazy() || FIXME 7/31 */ p->ticked() ? *p : /* FIXME 7/31 adds too many states p->greedy() ? p->lazy(0).greedy(false) : */ p->lazy(k->lazy())); // FIXME 7/18 ticked() preserves lookahead tail at '/' and ')'
#ifdef DEBUG
              DBGLOGN("lazy followpos(");
              DBGLOGPOS(*k);
              DBGLOGA(" ) = {");
              for (Positions::const_iterator q = j->second.begin(); q != j->second.end(); ++q)
                DBGLOGPOS(*q);
              DBGLOGA(" }");
#endif
            }
            i = j;
          }
          const Positions &follow = i->second;
          Chars chars;
          if (literal)
          {
            chars.insert(c);
          }
          else
          {
            switch (c)
            {
              case '.':
                if (opt_.s || is_modified('s', modifiers, loc))
                {
                  chars.insert(0, 0xff);
                }
                else
                {
                  chars.insert(0, 9);
                  chars.insert(11, 0xff);
                }
                break;
              case '^':
                chars.insert(opt_.m || is_modified('m', modifiers, loc) ? META_BOL : META_BOB);
                break;
              case '$':
                chars.insert(opt_.m || is_modified('m', modifiers, loc) ? META_EOL : META_EOB);
                break;
              default:
                if (c == '[' && escapes_at(loc, "AZBb<>ij") == '\0')
                {
                  compile_list(loc + 1, chars, modifiers);
                }
                else
                {
                  switch (escape_at(loc))
                  {
                    case 'i':
                      chars.insert(META_IND);
                      break;
                    case 'j':
                      chars.insert(META_DED);
                      break;
                    case 'A':
                      chars.insert(META_BOB);
                      break;
                    case 'Z':
                      chars.insert(META_EOB);
                      break;
                    case 'B':
                      chars.insert(k->anchor() ? META_NWB : META_NWE);
                      break;
                    case 'b':
                      if (k->anchor())
                        chars.insert(META_BWB, META_EWB);
                      else
                        chars.insert(META_BWE, META_EWE);
                      break;
                    case '<':
                      chars.insert(k->anchor() ? META_BWB : META_BWE);
                      break;
                    case '>':
                      chars.insert(k->anchor() ? META_EWB : META_EWE);
                      break;
                    case '\0': // no escape at current loc
                      if (std::isalpha(c) && (opt_.i || is_modified('i', modifiers, loc)))
                      {
                        chars.insert(std::toupper(c));
                        chars.insert(std::tolower(c));
                      }
                      else
                      {
                        chars.insert(c);
                      }
                      break;
                    default:
                      compile_esc(loc + 1, chars);
                  }
                }
            }
          }
          transition(moves, chars, follow);
        }
      }
    }
  }
  DBGLOG("END compile_transition()");
}

void Pattern::transition(
    Moves&           moves,
    const Chars&     chars,
    const Positions& follow) const
{
  Chars rest = chars;
  Moves::iterator i = moves.begin();
  Moves::const_iterator end = moves.end();
  while (i != end)
  {
    if (i->second == follow)
    {
      // chars += i->first; // FIXME ??? is this needed ??? NO! because ranges do not overlap so chars cannot intersect
      rest |= i->first;
      moves.erase(i++);
    }
    else if (chars.intersects(i->first))
    {
      Chars common(chars & i->first);
      if (is_subset(follow, i->second))
      {
        rest -= common;
        ++i;
      }
      else if (i->first == common && is_subset(i->second, follow))
      {
        moves.erase(i++);
      }
      else
      {
        rest -= common;
        i->first -= common;
        if (i->first.any()) // any bits or ranges?
        {
          Move back;
          back.first.swap(common);
          back.second = i->second;
          set_insert(back.second, follow);
          moves.push_back(back); // faster: C++11 emplace_back(Chars(), i->second); move.back().first.swap(common)
        }
        else
        {
          i->first.swap(common);
          set_insert(i->second, follow);
        }
        ++i;
      }
    }
    else
    {
      ++i;
    }
  }
  if (rest.any())
    moves.push_back(Move(rest, follow)); // faster: C++11 move.emplace_back(rest, follow)
}

Pattern::Char Pattern::compile_esc(Location loc, Chars& chars) const throw (Error)
{
  Char c = at(loc);
  if (c == '0')
  {
    c = static_cast<Char>(std::strtoul(rex_.substr(loc + 1, 3).c_str(), NULL, 8));
  }
  else if ((c == 'x' || c == 'u') && at(loc + 1) == '{')
  {
    c = static_cast<Char>(std::strtoul(rex_.c_str() + 2, NULL, 16));
  }
  else if (c == 'x' && std::isxdigit(at(loc + 1)))
  {
    c = static_cast<Char>(std::strtoul(rex_.substr(loc + 1, 2).c_str(), NULL, 16));
  }
  else if (c == 'c')
  {
    c = at(loc + 1) % 32;
  }
  else if (c == 'e')
  {
    c = 0x1b;
  }
  else if (c == '_')
  {
    posix(6 /* alpha */, chars);
  }
  else if (c == 'p' && at(loc + 1) == '{')
  {
    size_t i;
    for (i = 0; i < 14; ++i)
      if (eq_at(loc + 2, posix_class[i]))
        break;
    if (i < 14)
      posix(i, chars);
    else
      error(Error::REGEX_SYNTAX, "unrecognized character class", loc);
    return META_EOL;
  }
  else
  {
    static const char abtnvfr[] = "abtnvfr";
    const char *s = strchr(abtnvfr, c);
    if (s)
    {
      c = static_cast<Char>(s - abtnvfr + '\a');
    }
    else
    {
      static const char escapes[] = "__sSxX________hHdD__lL__uUwW";
      s = strchr(escapes, c);
      if (s)
      {
        posix((s - escapes) / 2, chars);
        if ((s - escapes) % 2)
          flip(chars);
        return META_EOL;
      }
    }
  }
  if (c <= 0xff)
    chars.insert(c);
  return c;
}

void Pattern::compile_list(Location loc, Chars& chars, const Map& modifiers) const throw (Error)
{
  bool complement = (at(loc) == '^');
  if (complement)
    ++loc;
  Char prev = META_BOL;
  Char lo = META_EOL;
  for (Char c = at(loc); c != '\0' && (c != ']' || prev == META_BOL); c = at(++loc))
  {
    if (c == '-' && !is_meta(prev) && is_meta(lo))
    {
      lo = prev;
    }
    else
    {
      Location c_loc;
      if (c == '[' && at(loc + 1) == ':' && (c_loc = find_at(loc + 2, ':')) != std::string::npos && at(c_loc + 1) == ']')
      {
        if (c_loc == loc + 3)
          c = compile_esc(loc + 2, chars);
        else
        {
          size_t i;
          for (i = 0; i < 14; ++i)
            if (eq_at(loc + 3, posix_class[i] + 1)) // ignore first letter (upper/lower) when matching
              break;
          if (i < 14)
            posix(i, chars);
          else
            error(Error::REGEX_SYNTAX, "unrecognized POSIX character class", loc);
          c = META_EOL;
        }
        loc = c_loc + 1;
      }
      else if (c == opt_.e && opt_.e != '\0' && !opt_.b)
      {
        c = compile_esc(loc + 1, chars);
        parse_esc(loc);
        --loc;
      }
      if (!is_meta(c))
      {
        if (!is_meta(lo))
        {
          if (lo <= c)
            chars.insert(lo, c);
          else
            error(Error::REGEX_LIST, "inverted character range in list", loc);
          if (opt_.i || is_modified('i', modifiers, loc))
          {
            for (Char a = lo; a <= c; ++a)
            {
              if (std::isupper(a))
                chars.insert(std::tolower(a));
              else if (std::islower(a))
                chars.insert(std::toupper(a));
            }
          }
          c = META_EOL;
        }
        else
        {
          if (std::isalpha(c) && (opt_.i || is_modified('i', modifiers, loc)))
          {
            chars.insert(std::toupper(c));
            chars.insert(std::tolower(c));
          }
          else
          {
            chars.insert(c);
          }
        }
      }
      prev = c;
      lo = META_EOL;
    }
  }
  if (!is_meta(lo))
    chars.insert('-');
  if (complement)
    flip(chars);
}

void Pattern::posix(size_t index, Chars& chars) const
{
  DBGLOG("posix(%lu)", index);
  switch (index)
  {
    case 0:
      chars.insert(0x00, 0x7f);
      break;
    case 1:
      chars.insert('\t', '\r');
      chars.insert(' ');
      chars.insert(0x85);
      break;
    case 2:
      chars.insert('0', '9');
      chars.insert('A', 'F');
      chars.insert('a', 'f');
      break;
    case 3:
      chars.insert(0x00, 0x1f);
      chars.insert(0x7f);
      break;
    case 4:
      chars.insert(' ', '~');
      break;
    case 5:
      chars.insert('0', '9');
      chars.insert('A', 'Z');
      chars.insert('a', 'z');
      break;
    case 6:
      chars.insert('A', 'Z');
      chars.insert('a', 'z');
      break;
    case 7:
      chars.insert('\t');
      chars.insert(' ');
      break;
    case 8:
      chars.insert('0', '9');
      break;
    case 9:
      chars.insert('!', '~');
      break;
    case 10:
      chars.insert('a', 'z');
      break;
    case 11:
      chars.insert('!', '/');
      chars.insert(':', '@');
      chars.insert('[', '`');
      chars.insert('{', '~');
      break;
    case 12:
      chars.insert('A', 'Z');
      break;
    case 13:
      chars.insert('0', '9');
      chars.insert('A', 'Z');
      chars.insert('a', 'z');
      chars.insert('_');
      break;
  }
}

void Pattern::flip(Chars& chars) const
{
#ifdef BITS
  chars.reserve(256).flip();
#else
  Chars flip;
  Char c = 0;
  for (Chars::const_iterator i = chars.begin(); i != chars.end(); ++i)
  {
    if (c < i->first)
      flip.insert(c, i->first - 1);
    c = i->second;
  }
  if (c <= 0xff)
    flip.insert(c, 0xff);
  chars.swap(flip);
#endif
}

void Pattern::assemble(State& start) throw (Error)
{
  DBGLOG("BEGIN assemble()");
  export_dfa(start);
  compact_dfa(start);
  encode_dfa(start);
  delete_dfa(start);
  export_code();
  DBGLOG("END assemble()");
}

void Pattern::compact_dfa(State& start)
{
  for (State *state = &start; state; state = state->next)
  {
    for (State::Edges::iterator i = state->edges.begin(); i != state->edges.end(); ++i)
    {
      Char hi = i->second.first;
      if (hi >= 0xff)
        break;
      State::Edges::iterator j = i;
      ++j;
      while (j != state->edges.end() && j->first <= hi + 1)
      {
        hi = j->second.first;
        if (j->second.second == i->second.second)
        {
          i->second.first = hi;
          state->edges.erase(j++);
        }
        else
        {
          ++j;
        }
      }
    }
  }
}

void Pattern::encode_dfa(State& start) throw (Error)
{
  nop_ = 0;
  for (State *state = &start; state; state = state->next)
  {
    state->index = nop_;
    Char hi = 0;
    for (State::Edges::const_iterator i = state->edges.begin(); i != state->edges.end(); ++i)
    {
      if (i->first == hi)
        hi = i->second.first + 1;
      ++nop_;
      if (is_meta(i->first))
        nop_ += i->second.first - i->first;
    }
    // add dead state only when needed
    if (hi <= 0xff)
    {
      state->edges[hi] = std::pair<Char,State*>(0xff, NULL);
      ++nop_;
    }
    nop_ += static_cast<Index>(state->heads.size() + state->tails.size() + (state->accept > 0 || state->redo));
    if (nop_ < state->index)
      error(Error::CODE_OVERFLOW, "out of code memory");
  }
  Opcode *opcode = new Opcode[nop_];
  opc_ = opcode;
  Index pc = 0;
  for (const State *state = &start; state; state = state->next)
  {
    if (state->redo)
      opcode[pc++] = opcode_redo();
    else if (state->accept > 0)
      opcode[pc++] = opcode_take(state->accept);
    for (Set::const_iterator i = state->tails.begin(); i != state->tails.end(); ++i)
      opcode[pc++] = opcode_tail(static_cast<Index>(*i));
    for (Set::const_iterator i = state->heads.begin(); i != state->heads.end(); ++i)
      opcode[pc++] = opcode_head(static_cast<Index>(*i));
    for (State::Edges::const_reverse_iterator i = state->edges.rbegin(); i != state->edges.rend(); ++i)
    {
      Char lo = i->first;
      Char hi = i->second.first;
      Index target_index = IMAX;
      if (i->second.second)
        target_index = i->second.second->index;
      if (!is_meta(lo))
      {
        opcode[pc++] = opcode_goto(lo, hi, target_index);
      }
      else
      {
        do
        {
          opcode[pc++] = opcode_goto(lo, lo, target_index);
        } while (++lo <= hi);
      }
      // else TODO in the future: wide char opcode??
    }
  }
}

void Pattern::delete_dfa(State& start)
{
  const State *state = start.next;
  while (state)
  {
    const State *next_state = state->next;
    delete state;
    state = next_state;
  }
  start.next = NULL;
}

void Pattern::export_dfa(const State& start) const
{
  for (std::vector<std::string>::const_iterator i = opt_.f.begin(); i != opt_.f.end(); ++i)
  {
    const std::string& filename = *i;
    size_t len = filename.length();
    if (len > 3 && filename.compare(len - 3, 3, ".gv") == 0)
    {
      FILE *fd;
      if (filename.compare(0, 7, "stdout.") == 0)
        fd = stdout;
      else if (filename.at(0) == '+')
        fd = ::fopen(filename.c_str() + 1, "a");
      else
        fd = ::fopen(filename.c_str(), "w");
      if (fd)
      {
        ::fprintf(fd, "digraph %s {\n\t\trankdir=LR;\n\t\tconcentrate=true;\n\t\tnode [fontname=\"ArialNarrow\"];\n\t\tedge [fontname=\"Courier\"];\n\n\t\tinit [root=true,peripheries=0,label=\"%s\",fontname=\"Courier\"];\n\t\tinit -> N%p;\n", opt_.n.empty() ? "FSM" : opt_.n.c_str(), opt_.n.c_str(), &start);
        for (const State *state = &start; state; state = state->next)
        {
          if (state == &start)
            ::fprintf(fd, "\n/*START*/\t");
          if (state->redo) // state->accept == IMAX)
            ::fprintf(fd, "\n/*REDO*/\t");
          else if (state->accept)
            ::fprintf(fd, "\n/*ACCEPT %hu*/\t", state->accept);
          for (Set::const_iterator i = state->heads.begin(); i != state->heads.end(); ++i)
            ::fprintf(fd, "\n/*HEAD %lu*/\t", *i);
          for (Set::const_iterator i = state->tails.begin(); i != state->tails.end(); ++i)
            ::fprintf(fd, "\n/*TAIL %lu*/\t", *i);
          if (state != &start && !state->accept && state->heads.empty() && state->tails.empty())
            ::fprintf(fd, "\n/*STATE*/\t");
          ::fprintf(fd, "N%p [label=\"", state);
#ifdef DEBUG
          size_t k = 1;
          size_t n = std::sqrt(state->size()) + 0.5;
          const char *sep = "";
          for (Positions::const_iterator i = state->begin(); i != state->end(); ++i)
          {
            ::fprintf(fd, "%s", sep);
            if (i->accept())
            {
              ::fprintf(fd, "(%hu)", i->accepts());
            }
            else
            {
              if (i->iter())
                ::fprintf(fd, "%hu.", i->iter());
              ::fprintf(fd, "%zu", i->loc());
            }
            if (i->lazy())
              ::fprintf(fd, "?%zu", i->lazy());
            if (i->anchor())
              ::fprintf(fd, "^");
            if (i->greedy())
              ::fprintf(fd, "!");
            if (i->ticked())
              ::fprintf(fd, "'");
            if (k++ % n)
              sep = " ";
            else
              sep = "\\n";
          }
          if ((state->accept && !state->redo) || !state->heads.empty() || !state->tails.empty())
            ::fprintf(fd, "\\n");
#endif
          if (state->accept && !state->redo) // state->accept != IMAX)
            ::fprintf(fd, "[%hu]", state->accept);
          for (Set::const_iterator i = state->tails.begin(); i != state->tails.end(); ++i)
            ::fprintf(fd, "%lu>", *i);
          for (Set::const_iterator i = state->heads.begin(); i != state->heads.end(); ++i)
            ::fprintf(fd, "<%lu", *i);
          if (state->redo) // state->accept != IMAX)
            ::fprintf(fd, "\",style=dashed,peripheries=1];\n");
          else if (state->accept) // state->accept != IMAX)
            ::fprintf(fd, "\",peripheries=2];\n");
          else if (!state->heads.empty())
            ::fprintf(fd, "\",style=dashed,peripheries=2];\n");
          else
            ::fprintf(fd, "\"];\n");
          for (State::Edges::const_iterator i = state->edges.begin(); i != state->edges.end(); ++i)
          {
            Char lo = i->first;
            Char hi = i->second.first;
            if (!is_meta(lo))
            {
              ::fprintf(fd, "\t\tN%p -> N%p [label=\"", state, i->second.second);
              if (lo >= '\a' && lo <= '\r')
                ::fprintf(fd, "\\\\%c", "abtnvfr"[lo - '\a']);
              else if (lo == '"')
                ::fprintf(fd, "\\\"");
              else if (lo == '\\')
                ::fprintf(fd, "\\\\");
              else if (std::isgraph(lo))
                ::fprintf(fd, "%c", lo);
              else if (lo < 8)
                ::fprintf(fd, "\\\\%u", lo);
              else
                ::fprintf(fd, "\\\\x%2.2x", lo);
              if (lo != hi)
              {
                ::fprintf(fd, "-");
                if (hi >= '\a' && hi <= '\r')
                  ::fprintf(fd, "\\\\%c", "abtnvfr"[hi - '\a']);
                else if (hi == '"')
                  ::fprintf(fd, "\\\"");
                else if (hi == '\\')
                  ::fprintf(fd, "\\\\");
                else if (std::isgraph(hi))
                  ::fprintf(fd, "%c", hi);
                else if (hi < 8)
                  ::fprintf(fd, "\\\\%u", hi);
                else
                  ::fprintf(fd, "\\\\x%2.2x", hi);
              }
              ::fprintf(fd, "\"];\n");
            }
            else
            {
              do
              {
                ::fprintf(fd, "\t\tN%p -> N%p [label=\"%s\",style=\"dashed\"];\n", state, i->second.second, meta_label[lo - META_MIN]);
              } while (++lo <= hi);
            }
          }
          if (state->redo) // state->accept == IMAX)
            ::fprintf(fd, "\t\tN%p -> R%p;\n\t\tR%p [peripheries=0,label=\"redo\"];\n", state, state, state);
        }
        ::fprintf(fd, "}\n");
        if (fd != stdout)
          ::fclose(fd);
      }
    }
  }
}

void Pattern::export_code() const
{
  if (!nop_)
    return;
  for (std::vector<std::string>::const_iterator i = opt_.f.begin(); i != opt_.f.end(); ++i)
  {
    const std::string& filename = *i;
    size_t len = filename.length();
    if ((len > 2 && filename.compare(len - 2, 2, ".h"  ) == 0)
     || (len > 4 && filename.compare(len - 4, 4, ".hpp") == 0)
     || (len > 4 && filename.compare(len - 4, 4, ".cpp") == 0)
     || (len > 3 && filename.compare(len - 3, 3, ".cc" ) == 0))
    {
      FILE *fd;
      if (filename.compare(0, 7, "stdout.") == 0)
        fd = stdout;
      else if (filename.at(0) == '+')
        fd = ::fopen(filename.c_str() + 1, "a");
      else
        fd = ::fopen(filename.c_str(), "w");
      if (fd)
      {
        ::fprintf(fd, "#ifndef REFLEX_CODE_DECL\n#include \"pattern.h\"\n#define REFLEX_CODE_DECL const reflex::Pattern::Opcode\n#endif\n\nREFLEX_CODE_DECL reflex_code_%s[%hu] =\n{\n", opt_.n.empty() ? "FSM" : opt_.n.c_str(), nop_);
        for (Index i = 0; i < nop_; ++i)
        {
          Opcode opcode = opc_[i];
          ::fprintf(fd, "  0x%08X, // %hu: ", opcode, i);
          Index index = index_of(opcode);
          if (is_opcode_redo(opcode))
          {
            ::fprintf(fd, "REDO\n");
          }
          else if (is_opcode_take(opcode))
          {
            ::fprintf(fd, "TAKE %u\n", index);
          }
          else if (is_opcode_tail(opcode))
          {
            ::fprintf(fd, "TAIL %u\n", index);
          }
          else if (is_opcode_head(opcode))
          {
            ::fprintf(fd, "HEAD %u\n", index);
          }
          else if (is_opcode_halt(opcode))
          {
            ::fprintf(fd, "HALT\n");
          }
          else
          {
            if (index == IMAX)
              ::fprintf(fd, "HALT ON ");
            else
              ::fprintf(fd, "GOTO %u ON ", index);
            Char lo = lo_of(opcode);
            if (!is_meta(lo))
            {
              if (lo >= '\a' && lo <= '\r')
                ::fprintf(fd, "\\%c", "abtnvfr"[lo - '\a']);
              else if (lo == '\\')
                ::fprintf(fd, "'\\'");
              else if (std::isgraph(lo))
                ::fprintf(fd, "%c", lo);
              else if (lo < 8)
                ::fprintf(fd, "\\%u", lo);
              else
                ::fprintf(fd, "\\x%2.2x", lo);
              Char hi = hi_of(opcode);
              if (lo != hi)
              {
                ::fprintf(fd, "-");
                if (hi >= '\a' && hi <= '\r')
                  ::fprintf(fd, "\\%c", "abtnvfr"[hi - '\a']);
                else if (hi == '\\')
                  ::fprintf(fd, "'\\'");
                else if (std::isgraph(hi))
                  ::fprintf(fd, "%c", hi);
                else if (hi < 8)
                  ::fprintf(fd, "\\%u", hi);
                else
                  ::fprintf(fd, "\\x%2.2x", hi);
              }
            }
            else
            {
              ::fprintf(fd, "%s", meta_label[lo - META_MIN]);
            }
            ::fprintf(fd, "\n");
          }
        }
        ::fprintf(fd, "};\n\n");
        if (fd != stdout)
          ::fclose(fd);
      }
    }
  }
}

void Pattern::Error::display(std::ostream& os) const
{
  os << "reflex::Pattern error ";
  if (loc)
  {
    size_t n = loc / 80;
    unsigned short r = static_cast<unsigned short>(loc % 80);
    os
      << "at "
      << loc
      << std::endl
      << pattern.rex_.substr(80*n, 79)
      << std::endl
      << std::setw(r + 4)
      << std::right
      << "^~~ ";
  }
  os << message << std::endl;
}

} // namespace reflex
