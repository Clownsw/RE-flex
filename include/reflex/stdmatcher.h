/******************************************************************************\
* Copyright (c) 2017, Robert van Engelen, Genivia Inc. All rights reserved.    *
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
@file      stdmatcher.h
@brief     C++11 std::regex-based matcher engines for pattern matching
@author    Robert van Engelen - engelen@genivia.com
@copyright (c) 2015-2017, Robert van Engelen, Genivia Inc. All rights reserved.
@copyright (c) BSD-3 License - see LICENSE.txt
*/

#ifndef REFLEX_STDMATCHER_H
#define REFLEX_STDMATCHER_H

#include <reflex/absmatcher.h>
#include <regex>

namespace reflex {

/// std matcher engine class implements reflex::PatternMatcher pattern matching interface with scan, find, split functors and iterators, using the C++11 std::regex library.
/** More info TODO */
class StdMatcher : public PatternMatcher<std::regex> {
 public:
  /// Construct matcher engine from a std::regex object or string regex, and an input character sequence.
  template<typename P> /// @tparam <P> pattern is a std::regex or a string regex
  StdMatcher(
      const P&     pat,           ///< a std::regex or a string regex for this matcher
      const Input& inp = Input(), ///< input character sequence for this matcher
      const char  *opt = NULL)    ///< option string of the form `(A|N|T(=[[:digit:]])?|;)*`
    :
      PatternMatcher(pat, inp, opt)
  {
    reset();
    buffer(); // no partial matching supported: buffer all input
  }
  /// Construct matcher engine from a std::regex object or string regex, and an input character sequence.
  template<typename P> /// @tparam <P> pattern is a std::regex or a string regex
  StdMatcher(
      const P     *pat,           ///< points to a std::regex or a string regex for this matcher
      const Input& inp = Input(), ///< input character sequence for this matcher
      const char  *opt = NULL)    ///< option string of the form `(A|N|T(=[[:digit:]])?|;)*`
    :
      PatternMatcher(pat, inp, opt)
  {
    reset();
    buffer(); // no partial matching supported: buffer all input
  }
  /// Reset this matcher's state to the initial state and when assigned new input.
  virtual void reset(const char *opt = NULL)
  {
    DBGLOG("StdMatcher::reset()");
    itr_ = fin_;
    PatternMatcher::reset(opt);
  }
  using PatternMatcher::input;
  /// Set the input character sequence for this matcher and reset the matcher.
  virtual AbstractMatcher& input(const Input& inp) ///< input character sequence for this matcher
    /// @returns this matcher.
  {
    DBGLOG("StdMatcher::input()");
    in = inp;
    reset();
    buffer(); // no partial matching supported: buffer all input
    return *this;
  }
  using PatternMatcher::pattern;
  /// Set the pattern to use with this matcher as a shared pointer to another matcher pattern.
  virtual PatternMatcher& pattern(const StdMatcher& matcher) ///< the other matcher
    /// @returns this matcher.
  {
    opt_ = matcher.opt_;
    flg_ = matcher.flg_;
    return this->pattern(matcher.pattern());
  }
  /// Set the pattern to use with this matcher (the given pattern is shared and must be persistent), overrides the ECMA/POSIX/AWK syntax option.
  virtual PatternMatcher& pattern(const Pattern *pat) ///< std::regex for this matcher
    /// @returns this matcher.
  {
    itr_ = fin_;
    return PatternMatcher::pattern(pat);
  }
  /// Set the pattern to use with this matcher (the given pattern is shared and must be persistent), overrides the ECMA/POSIX/AWK syntax option.
  virtual PatternMatcher& pattern(const Pattern& pat) ///< std::regex for this matcher
    /// @returns this matcher.
  {
    itr_ = fin_;
    return PatternMatcher::pattern(pat);
  }
  /// Set the pattern from a regex string to use with this matcher, inherits the ECMA/POSIX syntax option.
  virtual PatternMatcher& pattern(const char *pat) ///< regex string to instantiate internal pattern object
    /// @returns this matcher.
  {
    itr_ = fin_;
    PatternMatcher::pattern(new std::regex(pat, pattern().flags()));
    own_ = true;
    return *this;
  }
  /// Set the pattern from a regex string to use with this matcher, inherits the ECMA/POSIX syntax option.
  virtual PatternMatcher& pattern(const std::string& pat) ///< regex string to instantiate internal pattern object
    /// @returns this matcher.
  {
    itr_ = fin_;
    PatternMatcher::pattern(new std::regex(pat, pattern().flags()));
    own_ = true;
    return *this;
  }
 protected:
  /// The match method Const::SCAN, Const::FIND, Const::SPLIT, or Const::MATCH, implemented with std::regex.
  virtual size_t match(Method method)
    /// @returns nonzero when input matched the pattern using method Const::SCAN, Const::FIND, Const::SPLIT, or Const::MATCH.
  {
    DBGLOG("BEGIN StdMatcher::match(%d)", method);
    bool bob = at_bob();
    txt_ = buf_ + cur_; // set first of text(), cur_ was last pos_, or cur_ was set with more()
    cur_ = pos_; // reset cur_ when changed in more()
    if (pos_ < end_) // if next pos_ is not hitting the end_ then
      buf_[pos_] = chr_; // last of text() was set to NUL in buf_[], set it back
    if (itr_ != fin_) // if regex iterator is still valid then
    {
      if ((*itr_)[0].second == buf_ + pos_) // if last of regex iterator is still valid in buf_[] then
      {
        DBGLOGN("Continue iterating, pos = %zu", pos_);
        ++itr_;
        if (itr_ != fin_) // set pos_ to last of the match
        {
          pos_ = (*itr_)[0].second - buf_;
          if (pos_ == cur_ && pos_ < end_) // same pos as previous?
          {
            ++txt_;
            new_itr(method, false);
            if (itr_ != fin_)
            {
              pos_ = (*itr_)[0].second - buf_;
              DBGLOGN("Force iterator forward pos = %zu", pos_);
            }
          }
        }
      }
      else
      {
        itr_ = fin_;
      }
    }
    while (pos_ == end_ || itr_ == fin_) // fetch more data while pos_ is hitting the end_ or no iterator
    {
      if (pos_ == end_ && !eof_)
      {
        if (grow()) // make sure we have enough storage to read input
          itr_ = fin_; // buffer shifting/growing invalidates iterator
        end_ += get(buf_ + end_, blk_ ? blk_ : max_ - end_); // get() may also wrap()
      }
      if (pos_ == end_) // if pos_ is hitting the end_ then
      {
        if (method == Const::SPLIT)
        {
          DBGLOGN("Split end");
          if (eof_)
          {
            cap_ = 0;
            len_ = 0;
          }
          else
          {
            if (itr_ != fin_ && (*itr_)[0].matched && cur_ != pos_)
            {
              size_t n = (*itr_).size();
              for (cap_ = 1; cap_ < n && !(*itr_)[cap_].matched; ++cap_)
                continue; // set cap_ to the capture index
              len_ = (*itr_)[0].first - txt_; // size() spans txt_ to cur_ in buf_[]
            }
            else
            {
              DBGLOGN("Matched empty end");
              cap_ = Const::EMPTY;
              len_ = pos_ - (txt_ - buf_); // size() spans txt_ to cur_ in buf_[]
              eof_ = true;
            }
            itr_ = fin_;
            cur_ = pos_;
            buf_[txt_ - buf_ + len_] = '\0';
            DBGLOGN("Split: act = %zu txt = '%s' len = %zu pos = %zu eof = %d", cap_, txt_, len_, pos_, eof_ == true);
          }
          DBGLOG("END StdMatcher::match()");
          return cap_;
        }
        eof_ = true;
        if (method == Const::FIND && opt_.N)
          return 0;
        if (itr_ != fin_)
          break; // OK if iterator is still valid
      }
      new_itr(method, bob); // need new iterator
      if (itr_ != fin_)
      {
        DBGLOGN("Match, pos = %zu", pos_);
        pos_ = (*itr_)[0].second - buf_; // set pos_ to last of the match
      }
      else // no match
      {
        if ((method == Const::SCAN || method == Const::MATCH))
        {
          pos_ = cur_;
          len_ = 0;
          cap_ = 0;
          chr_ = static_cast<unsigned char>(buf_[pos_]);
          buf_[pos_] = '\0';
          DBGLOGN("No match, pos = %zu", pos_);
          DBGLOG("END StdMatcher::match()");
          return 0;
        }
        pos_ = end_;
        if (eof_)
        {
          len_ = 0;
          cap_ = 0;
          DBGLOGN("No match at EOF, pos = %zu", pos_);
          DBGLOG("END StdMatcher::match()");
          return 0;
        }
      }
    }
    if (method == Const::SPLIT)
    {
      DBGLOGN("Split match");
      size_t n = (*itr_).size();
      for (cap_ = 1; cap_ < n && !(*itr_)[cap_].matched; ++cap_)
        continue; // set cap_ to the capture index
      len_ = (*itr_)[0].first - txt_; // cur_ - (txt_ - buf_); // size() spans txt_ to cur_ in buf_[]
      set_current(pos_);
      buf_[txt_ - buf_ + len_] = '\0';
      DBGLOGN("Split: act = %zu txt = '%s' len = %zu pos = %zu", cap_, txt_, len_, pos_);
      DBGLOG("END StdMatcher::match()");
      return cap_;
    }
    else if (!(*itr_)[0].matched || (buf_ + cur_ != (*itr_)[0].first && method != Const::FIND)) // if no match at first and we're not searching then
    {
      itr_ = fin_;
      pos_ = cur_;
      len_ = 0;
      cap_ = 0;
      chr_ = static_cast<unsigned char>(buf_[pos_]);
      buf_[pos_] = '\0';
      DBGLOGN("No match, pos = %zu", pos_);
      DBGLOG("END StdMatcher::match()");
      return 0;
    }
    if (method == Const::FIND)
      txt_ = (*itr_)[0].first;
    size_t n = (*itr_).size();
    for (cap_ = 1; cap_ < n && !(*itr_)[cap_].matched; ++cap_)
      continue; // set cap_ to the capture group index
    set_current(pos_);
    buf_[pos_] = '\0';
    len_ = cur_ - (txt_ - buf_); // size() spans txt_ to cur_ in buf_[]
    if (len_ == 0 && cap_ != 0 && opt_.N && pos_ + 1 == end_)
      set_current(end_);
    DBGLOGN("Accept: act = %zu txt = '%s' len = %zu", cap_, txt_, len_);
    DBGCHK(len_ != 0 || method == Const::MATCH || (method == Const::FIND && opt_.N));
    DBGLOG("END StdMatcher::match()");
    return cap_;
  }
  /// Create a new std::regex iterator to (continue to) advance over input.
  inline void new_itr(Method method, bool bob)
  {
    DBGLOGN("New iterator");
    bool bol = bob || at_bol();
    bool eow = isword(got_);
    std::regex_constants::match_flag_type flg = flg_;
    if (!bol)
      flg |= std::regex_constants::match_not_bol;
    if (eow)
      flg |= std::regex_constants::match_not_bow;
    if (method == Const::SCAN)
      flg |= std::regex_constants::match_continuous | std::regex_constants::match_not_null;
    else if (method == Const::FIND && !opt_.N)
      flg |= std::regex_constants::match_not_null;
    else if (method == Const::MATCH)
      flg |= std::regex_constants::match_continuous;
    itr_ = std::cregex_iterator(txt_, buf_ + end_, *pat_, flg);
  }
  std::regex_constants::match_flag_type flg_; ///< std::regex match flags
  std::cregex_iterator                  itr_; ///< const std::regex iterator
  std::cregex_iterator                  fin_; ///< const std::regex iterator final end
};

/// std matcher engine class, extends reflex::StdMatcher for ECMA std::regex::ECMAScript regex matching.
/**
std::regex with ECMAScript std::regex::ECMAScript.
*/
class StdEcmaMatcher : public StdMatcher {
 public:
  /// Construct an ECMA matcher engine from a std::regex pattern and an input character sequence.
  StdEcmaMatcher(
      const char  *pat,           ///< a string regex for this matcher
      const Input& inp = Input(), ///< input character sequence for this matcher
      const char  *opt = NULL)    ///< option string of the form `(A|N|T(=[[:digit:]])?|;)*`
    :
      StdMatcher(new std::regex(pat, std::regex::ECMAScript), inp, opt)
  {
    own_ = true;
  }
  /// Construct an ECMA matcher engine from a std::regex pattern and an input character sequence.
  StdEcmaMatcher(
      const std::string& pat,           ///< a string regex for this matcher
      const Input&       inp = Input(), ///< input character sequence for this matcher
      const char        *opt = NULL)    ///< option string of the form `(A|N|T(=[[:digit:]])?|;)*`
    :
      StdMatcher(new std::regex(pat, std::regex::ECMAScript), inp, opt)
  {
    own_ = true;
  }
  using StdMatcher::pattern;
  /// Set the pattern to use with this matcher (the given pattern is shared and must be persistent), fails when a POSIX std::regex is given.
  virtual PatternMatcher& pattern(const Pattern& pat) ///< std::regex for this matcher
    /// @returns this matcher.
  {
    ASSERT(!(pat.flags() & (std::regex::basic | std::regex::extended | std::regex::awk)));
    itr_ = fin_;
    return StdMatcher::pattern(pat);
  }
  /// Set the pattern to use with this matcher (the given pattern is shared and must be persistent), fails when a POSIX std::regex is given.
  virtual PatternMatcher& pattern(const Pattern *pat) ///< std::regex for this matcher
    /// @returns this matcher.
  {
    ASSERT(!(pat.flags() & (std::regex::basic | std::regex::extended | std::regex::awk)));
    itr_ = fin_;
    return StdMatcher::pattern(pat);
  }
};

/// std matcher engine class, extends reflex::StdMatcher for POSIX ERE std::regex::awk regex matching.
/**
std::regex with POSIX ERE std::regex::awk.
*/
class StdPosixMatcher : public StdMatcher {
 public:
  /// Construct a POSIX matcher engine from a string regex pattern and an input character sequence.
  StdPosixMatcher(
      const char  *pat,           ///< a string regex for this matcher
      const Input& inp = Input(), ///< input character sequence for this matcher
      const char  *opt = NULL)    ///< option string of the form `(A|N|T(=[[:digit:]])?|;)*`
    :
      StdMatcher(new std::regex(pat, std::regex::awk), inp, opt)
  {
    own_ = true;
  }
  /// Construct a POSIX ERE matcher engine from a string regex pattern and an input character sequence.
  StdPosixMatcher(
      const std::string& pat,           ///< a string regex for this matcher
      const Input&       inp = Input(), ///< input character sequence for this matcher
      const char        *opt = NULL)    ///< option string of the form `(A|N|T(=[[:digit:]])?|;)*`
    :
      StdMatcher(new std::regex(pat, std::regex::awk), inp, opt)
  {
    own_ = true;
  }
  using StdMatcher::pattern;
  /// Set the pattern to use with this matcher (the given pattern is shared and must be persistent), fails when a non-POSIX ERE std::regex is given.
  virtual PatternMatcher& pattern(const Pattern& pat) ///< std::regex for this matcher
    /// @returns this matcher.
  {
    ASSERT(pat.flags() & std::regex::awk);
    itr_ = fin_;
    return StdMatcher::pattern(pat);
  }
  /// Set the pattern to use with this matcher (the given pattern is shared and must be persistent), fails when a non-POSIX ERE std::regex is given.
  virtual PatternMatcher& pattern(const Pattern *pat) ///< std::regex for this matcher
    /// @returns this matcher.
  {
    ASSERT(pat.flags() & std::regex::awk);
    itr_ = fin_;
    return StdMatcher::pattern(pat);
  }
};

} // namespace reflex

#endif

