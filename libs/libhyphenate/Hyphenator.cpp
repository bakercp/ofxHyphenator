/* libhyphenate: A TeX-like hyphenation algorithm.
 * Copyright (C) 2007 Steve Wolter
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301  USA
 *
 * If you have any questions, feel free to contact me:
 * http://swolter.sdf1.org
 **/
#include "Hyphenator.h"
#include <stdexcept>
#include <iostream>
#include <fstream>
#include <vector>
#include <map>
#include <memory>
#include <ctype.h>
#include <stdlib.h>

//#include <iconv.h>
#include <errno.h>
//#include "gunicode.h"

#include "HyphenationRule.h"
#include "HyphenationTree.h"

#define UTF8_MAX 6

using namespace std;
using namespace RFC_3066;
using namespace Hyphenate;

/** The hyphenation table parser. */
static auto_ptr<HyphenationTree> read_hyphenation_table(const char *filename) {
   ifstream i (filename, fstream::in);
   auto_ptr<HyphenationTree> output(new HyphenationTree());
   output->loadPatterns(i);

   return output;
}

/** Build a hyphenator for the given language. The hyphenation
   *  patterns for the language will loaded from a file named like
   *  the language string or any prefix of it. The file will be
   *  located in the directory given by the environment variable
   *  LIBHYPHENATE_PATH or, if this is empty, in the compiled-in
   *  pattern directory which defaults to 
   *  /usr/local/share/libhyphenate/patterns .
   *
   * \param lang The language for which hyphenation patterns will be
   *             loaded. */
Hyphenate::Hyphenator::Hyphenator(const RFC_3066::Language& lang) {
   setlocale(LC_CTYPE, "");
   string path = "";

   if (getenv("LIBHYPHENATE_PATH")) {
      path = getenv("LIBHYPHENATE_PATH");
   }

#ifdef LIBHYPHENATE_DEFAULT_PATH
   if (path == "")
      path = LIBHYPHENATE_DEFAULT_PATH;
#endif

   path += "/";

   string filename = lang.find_suitable_file(path);
   dictionary = read_hyphenation_table(filename.c_str());
}

/** Build a hyphenator from the patterns in the file provided. */
Hyphenate::Hyphenator::Hyphenator(const char *filename) {
   dictionary = read_hyphenation_table(filename);
}

Hyphenate::Hyphenator::Hyphenator(const std::string& dict) {
    std::stringstream ss;
    ss << dict;
    auto_ptr<HyphenationTree> output(new HyphenationTree());
    output->loadPatterns(ss);
    dictionary = output;
}


Hyphenator::~Hyphenator() {}




std::string Hyphenator::hyphenate
         (const std::string &word, const std::string &hyphen)
         throw(std::domain_error)
{
   string result;
   unsigned int word_start = -1;

   if ( ! ofUTF8::isValid(word) )
      throw std::domain_error(
               "Please supply a valid UTF-8 string for hyphenation.");

   /* Go through the input. All non-alpha characters are added to the
    * output immediately, and words are hyphenated and then added. */
   for (uint i = 0; i < word.size(); i++) {
      /* Skip UTF-8 tail bytes. */
      if ( (word[i] & 0xC0) == 0x80)
         ;
       
       
      else {
         bool isalpha = ofUnicode::isAlpha(ofUTF8::get(word.c_str()+i));

         if (word_start == string::npos && isalpha)
            word_start = i;
         else if (word_start != string::npos && !isalpha) {
            result += 
               hyphenate_word(word.substr(word_start, i-word_start), hyphen);
            word_start = string::npos;
         }
      }

      if (word_start == string::npos)
         result += word[i];
   }
   if (word_start != string::npos)
      result += hyphenate_word(word.substr(word_start), hyphen);

   return result;
}

std::string Hyphenator::hyphenate_word
         (const std::string &word, const std::string &hyphen)
         throw(std::domain_error)
{
   if ( ! ofUTF8::isValid(word) )
      throw std::domain_error(
               "Please supply a valid UTF-8 string for hyphenation.");

   auto_ptr<vector<const HyphenationRule*> > rules = 
      dictionary->applyPatterns(word);

   /* Build our result string. Of course, we _could_ insert characters in
    * w, but that would be highly inefficient. */
   string result;

   int acc_skip = 0;
   for (uint i = 0; i < word.size(); i++) {
      if ((*rules)[i] != NULL)
	 acc_skip += (*rules)[i]->apply(result, hyphen);

      if (acc_skip > 0)
         acc_skip--;
      else
         result += word[i];
   }
   
   return result;
}

pair<std::string,std::string> Hyphenator::hyphenate_at
         (const std::string &src, const std::string &hyphen, size_t len)
         throw(std::domain_error)
{
   if ( ! ofUTF8::isValid(src) )
      throw std::domain_error(
               "Please supply a valid UTF-8 string for hyphenation.");

   /* First of all, find the word which needs to be hyphenated. */
   const ofChar *cur = src.c_str();
   for (uint i = 0; i < len; i++)
      cur = ofUTF8::next(cur);
    
   const ofChar *next = cur;
   if (!ofUnicode::isSpace(ofUTF8::get(next)))
      next = ofUTF8::next(next);
   pair<string,string> result;

    if ( ofUnicode::isSpace(ofUTF8::get(next)) ) {
      /* We are lucky: There is a space we can hyphenate at. */
      
      /* We leave no spaces at the end of a line: */
      while ( ofUnicode::isSpace(ofUTF8::get(cur)) )
         cur = ofUTF8::prior(cur);
      int len = cur - src.c_str() + 1;
      result.first = src.substr(0, len);

      /* Neither do we leave spaces at the beginning of the next. */
      while ( ofUnicode::isSpace(ofUTF8::get(next)) )
         next = ofUTF8::next(next);
      result.second = src.substr( next - src.c_str() );

   } else {
      /* We can hyphenate at hyphenation points in words or at spaces, whatever
       * comes earlier. We will check all words here in the loop. */
      const ofChar *border = cur;
      while (true) {
         /* Find the start of a word first. */
          
         bool in_word = ofUnicode::isAlpha(ofUTF8::get(cur));
         const ofChar *word_start = NULL;
         while ( cur > src.c_str() ) {
            cur = ofUTF8::prior(cur);
            ofUniChar ch = ofUTF8::get(cur);

            if (in_word && (!ofUnicode::isAlpha(ch))) {
               /* If we have a word, try hyphenating it.*/
               word_start = ofUTF8::next(cur);
               break;
            } else if (ofUnicode::isSpace(ch)) {
               break;
            } else if (!in_word && ofUnicode::isAlpha(ch))
               in_word = true;

            if (cur == src.c_str() && in_word)
               word_start = cur;
         }
         
         /* There are two reasons why we may have left the previous loop with-
          * out result:
          * Either because our word goes all the way to the first character,
          * or because we found whitespace. */
         /* In the first case, there is nothing really hyphenateable. */
         if (word_start != NULL) {
            /* We have the start of a word, now look for the character after
            * the end. */
            const ofChar *word_end = word_start;
            while ( ofUnicode::isAlpha(ofUTF8::get(word_end)) )
               word_end = ofUTF8::next(word_end);

            /* Build the substring consisting of the word. */
            string word;
            for (const ofChar *i = word_start; i < word_end; i++)
               word += *i;

            /* Hyphenate the word. */
            auto_ptr<vector<const HyphenationRule*> > rules = 
               dictionary->applyPatterns(word);

            /* Determine the index of the latest hyphenation that will still
            * fit. */
            int latest_possible_hyphenation = -1;
            int earliest_hyphenation = -1;
            for (int i = 0; i < (int)rules->size(); i++)
               if ((*rules)[i] != NULL) {
                  if (earliest_hyphenation == -1)
                     earliest_hyphenation = i;
                  if (word_start + i + 
                        (*rules)[i]->spaceNeededPreHyphen() + hyphen.length()
                        <= border) 
                  {
                     if (i > latest_possible_hyphenation) {
                        latest_possible_hyphenation = i;
                     }
                  } else
                     break;
               }

            bool have_space = false;
            for (const ofChar *i = src.c_str(); i <= word_start; i = ofUTF8::next(i))
               if (ofUnicode::isSpace(ofUTF8::get(i))) {
                  have_space = true;
                  break;
               }
            if (latest_possible_hyphenation == -1 && !have_space)
               latest_possible_hyphenation = earliest_hyphenation;

            /* Apply the best hyphenation, if any. */
            if (latest_possible_hyphenation >= 0) {
               int i = latest_possible_hyphenation;
               result.first = src.substr(0, word_start-src.c_str()+i);
               (*rules)[i]->apply_first(result.first, hyphen);
               int skip = (*rules)[i]->apply_second(result.second);
               const ofChar *after_hyphen = word_start + i + skip;
               result.second += string(after_hyphen);
               break;
            }
         }

         if (cur == src.c_str()) {
            /* We cannot hyphenate at all, so leave the first block standing
             * and move to its end. */
            const ofChar *eol = cur;
            while (*eol != 0 && !ofUnicode::isSpace(ofUTF8::get(eol)))
               eol = ofUTF8::next(eol);

            result.first = src.substr(0, eol-src.c_str()+1);
            while (*eol != 0 && ofUnicode::isSpace(ofUTF8::get(eol)))
               eol = ofUTF8::next(eol);
            result.second = string(eol);
            break;
         } else if (ofUnicode::isSpace(ofUTF8::get(cur))) {
            /* eol is the end of the previous line, bol the start of the
               * next. */
            const ofChar *eol = cur, *bol = cur;
            while(ofUnicode::isSpace(ofUTF8::get(eol)))
               eol = ofUTF8::prior(eol);
            while(ofUnicode::isSpace(ofUTF8::get(bol)))
               bol = ofUTF8::next(bol);
            
            result.first  = src.substr(0, eol - src.c_str() + 1);
            result.second = string(bol);
            break;
         }
      }
   }

   return result;
}

std::auto_ptr<std::vector<const HyphenationRule*> > 
   Hyphenate::Hyphenator::applyHyphenationRules(const std::string& word)
{
   if ( ! ofUTF8::isValid(word) )
      throw std::domain_error(
               "Please supply a valid UTF-8 string for hyphenation.");

   return dictionary->applyPatterns(word);
}
