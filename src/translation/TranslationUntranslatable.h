#pragma once

#include <string>

namespace translation {

// Decides whether a whole source segment is "untranslatable": a URL, a file
// path, a bare host or e-mail address, a hash/UUID, a version literal, or a
// single code identifier token.
//
// Such segments are kept verbatim by the coordinator instead of being sent to
// the model. They carry nothing to translate, and asking a model for them only
// creates failure modes: the response contract requires one entry per requested
// id, so the cheapest way for the model to satisfy it is an empty "text" for a
// path -- which is a ContentContract failure and a wasted retry.
//
// The predicate is deliberately conservative, because the two mistakes are not
// symmetric: a wrong "translatable" verdict costs one request, while a wrong
// "untranslatable" verdict silently leaves a sentence untranslated. Anything
// ambiguous, anything containing CJK text, and anything containing whitespace
// stays translatable.
bool IsUntranslatableSegment(const std::wstring& text);

} // namespace translation
