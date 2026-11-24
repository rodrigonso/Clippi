#pragma once

#include "base/arena.h"
#include "base/array.h"
#include "base/str.h"

// journal.jsonl is written by Clippi, never the model.

constexpr u32 WIKI_SUMMARY_MAX_BYTES = 160;
constexpr u32 WIKI_SLUG_MAX_BYTES = 64;
constexpr u64 WIKI_PAGE_MAX_BYTES = 8 * 1024;
constexpr u64 WIKI_TOTAL_MAX_BYTES = 200 * 1024;
constexpr u32 WIKI_PINNED_MAX_BYTES = 4 * 1024;
constexpr u32 WIKI_CATALOG_MAX_LINES = 200;
constexpr u32 WIKI_CATALOG_MAX_BYTES = 25 * 1024;
constexpr u32 WIKI_MAX_PAGES = 1000;

enum class WikiPageType : u8 { Unknown, User, Feedback, Project, Reference, Topic };

struct WikiLocation {
  StrView Repository;
  StrView Root;
  StrView Pages;
  StrView Journal;
  StrView DreamDir;
};

StrView Wiki_DefaultStore(Arena* arena);

bool Wiki_Locate(Arena* arena, StrView store, StrView workingDirectory,
                 WikiLocation* out);

struct WikiPage {
  StrView Slug;
  StrView Title;
  StrView Summary;
  StrView TypeText;
  WikiPageType Type;
  bool HasFrontmatter;
  bool Pinned;
  StrView Body;
  u64 Bytes;
  u64 ModifiedMs;
  Array<StrView> Links;
};

struct Wiki {
  Array<WikiPage> Pages;
  u64 TotalBytes;
  u32 PinnedPages;
  u64 PinnedBytes;
};

bool Wiki_ValidSlug(StrView slug);

// `ModifiedMs` is left to the caller.
void Wiki_ParsePage(Arena* arena, StrView slug, StrView content, WikiPage* out);

void Wiki_ParseLinks(Arena* arena, StrView body, Array<StrView>* out);

void Wiki_Scan(Arena* arena, StrView pagesDir, Wiki* out);

const WikiPage* Wiki_Find(const Wiki* wiki, StrView slug);

enum class WikiIssueKind : u8 {
  InvalidSlug,
  MissingFrontmatter,
  MissingTitle,
  InvalidType,
  MissingSummary,
  LongSummary,
  BrokenLink,
  PageTooLarge,
  WikiTooLarge,
  PinnedTooLarge,
  Orphan,
  DuplicateTitle,
};

struct WikiIssue {
  WikiIssueKind Kind;
  StrView Slug;
  StrView Message;
  bool Error;
};

void Wiki_LintPage(Arena* arena, const Wiki* wiki, const WikiPage* page,
                   Array<WikiIssue>* out);
void Wiki_Lint(Arena* arena, const Wiki* wiki, Array<WikiIssue>* out);
u32 Wiki_ErrorCount(const Array<WikiIssue>* issues);
void Wiki_AppendIssues(StrBuf* out, const Array<WikiIssue>* issues);

struct WikiCatalogStats {
  u32 Listed;
  u32 Omitted;
};

void Wiki_AppendCatalog(Arena* arena, StrBuf* out, const Wiki* wiki,
                        WikiCatalogStats* stats);

void Wiki_AppendPromptBlock(Arena* arena, StrBuf* out, const Wiki* wiki,
                            StrView pagesDir, bool writable,
                            WikiCatalogStats* stats);

enum class WikiJournalKind : u8 { Unknown, Session, Page, Dream, Note };

constexpr u32 WIKI_NOTE_MAX_BYTES = 2000;

struct WikiJournalEntry {
  WikiJournalKind Kind;
  u64 TimeMs;
  StrView SessionId;
  StrView ProjectKey;
  StrView WorkingDirectory;
  StrView TranscriptPath;
  StrView Slug;
  StrView ToolCallId;
  StrView Status;
  StrView Text;
  u32 PagesChanged;
};

bool WikiJournal_Append(Arena* scratch, StrView journalPath,
                        const WikiJournalEntry* entry);

// `*outEnd` follows the last complete line; a partial line is read next time.
void WikiJournal_Read(Arena* arena, StrView journalPath, u64 offset,
                      Array<WikiJournalEntry>* out, u64* outEnd);
