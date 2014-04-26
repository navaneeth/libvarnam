/* Functions to handle the words store
 *
 * Copyright (C) Navaneeth.K.N
 *
 * This is part of libvarnam. See LICENSE.txt for the license
 */

#include <assert.h>
#include <string.h>

#include "symbol-table.h"
#include "util.h"
#include "vtypes.h"
#include "result-codes.h"
#include "api.h"
#include "token.h"
#include "rendering.h"
#include "varray.h"
#include "vword.h"
#include "words-table.h"

#define MINIMUM_CHARACTER_LENGTH_FOR_SUGGESTION 3

int
vwt_ensure_schema_exists(varnam *handle)
{
    const char *pragmas =
        "pragma page_size=4096;"
        "pragma journal_mode=wal;";

    const char *tables =
        "create table if not exists metadata (key TEXT UNIQUE, value TEXT);"
        "create table if not exists words (id integer primary key, word text unique, confidence integer default 1, learned_on integer);"
        "create table if not exists patterns_content (pattern text, word_id integer, learned integer default 0, primary key(pattern, word_id))without rowid;";

    char *zErrMsg = 0;
    int rc;

    rc = sqlite3_exec(v_->known_words, pragmas, NULL, 0, &zErrMsg);
    if( rc != SQLITE_OK ){
        set_last_error (handle, "Failed to initialize file for storing known words. Pragma setting failed. : %s", zErrMsg);
        sqlite3_free(zErrMsg);
        return VARNAM_ERROR;
    }

    rc = sqlite3_exec(v_->known_words, tables, NULL, 0, &zErrMsg);
    if( rc != SQLITE_OK ){
        set_last_error (handle, "Failed to initialize file for storing known words. Schema creation failed. : %s", zErrMsg);
        sqlite3_free(zErrMsg);
        return VARNAM_ERROR;
    }

    return VARNAM_SUCCESS;
}

static int
execute_sql(varnam *handle, sqlite3 *db, const char *sql)
{
    char *zErrMsg = 0;
    int rc;

    rc = sqlite3_exec(db, sql, NULL, 0, &zErrMsg);
    if( rc != SQLITE_OK ){
        set_last_error (handle, "Failed to write : %s", zErrMsg);
        sqlite3_free(zErrMsg);
        return VARNAM_ERROR;
    }

    return VARNAM_SUCCESS;
}

int
vwt_start_changes(varnam *handle)
{
    assert (v_->known_words);
    return execute_sql(handle, v_->known_words, "BEGIN;");
}

int
vwt_end_changes(varnam *handle)
{
    assert (v_->known_words);
    return execute_sql(handle, v_->known_words, "COMMIT;");
}

int
vwt_discard_changes(varnam *handle)
{
    assert (v_->known_words);
    return execute_sql(handle, v_->known_words, "ROLLBACK;");
}

int
vwt_optimize_for_huge_transaction(varnam *handle)
{
    assert (handle);
    assert (v_->known_words);

    return execute_sql (handle, v_->known_words, "PRAGMA synchronous = OFF;");
}

int
vwt_turn_off_optimization_for_huge_transaction(varnam *handle)
{
    /* const char *sql = */
    /*     "pragma journal_mode=wal;"; */
    /* assert (handle); */
    /* assert (v_->known_words); */

    /* return execute_sql (handle, v_->known_words, sql); */
    return VARNAM_SUCCESS;
}

int
vwt_compact_file (varnam *handle)
{
    /*const char *sql =*/
        /*"VACUUM;";*/

    /*assert (handle);*/
    /*assert (v_->known_words);*/

    /*return execute_sql (handle, v_->known_words, sql);*/
    /* Not doing any compacting */
    return VARNAM_SUCCESS;
}

int
vwt_persist_pattern(varnam *handle, const char *pattern, sqlite3_int64 word_id, bool is_prefix)
{
    int rc;
    const char *sql = "insert or ignore into patterns_content (pattern, word_id) values (trim(lower(?1)), ?2)";

    assert (v_->known_words);

#ifdef _VARNAM_VERBOSE
    if (!is_prefix) printf(" %s\n", pattern);
#endif

    if (v_->learn_pattern == NULL)
    {
        rc = sqlite3_prepare_v2( v_->known_words, sql, -1, &v_->learn_pattern, NULL );
        if (rc != SQLITE_OK) {
            set_last_error (handle, "Failed to learn word : %s", sqlite3_errmsg(v_->known_words));
            sqlite3_reset (v_->learn_pattern);
            return VARNAM_ERROR;
        }
    }

    sqlite3_bind_text  (v_->learn_pattern, 1, pattern, -1, NULL);
    sqlite3_bind_int64 (v_->learn_pattern, 2, word_id);

    rc = sqlite3_step (v_->learn_pattern);
    if (rc != SQLITE_DONE) {
        set_last_error (handle, "Failed to learn pattern : %s", sqlite3_errmsg(v_->known_words));
        sqlite3_reset (v_->learn_pattern);
        return VARNAM_ERROR;
    }
    sqlite3_reset (v_->learn_pattern);

    if (!is_prefix)
    {
        if (v_->update_learned_flag == NULL)
        {
            rc = sqlite3_prepare_v2( v_->known_words, "update patterns_content set learned = 1 where pattern = trim(lower(?1)) and word_id = ?2 and learned = 0",
                                     -1, &v_->update_learned_flag, NULL );
            if (rc != SQLITE_OK) {
                set_last_error (handle, "Failed to learn word : %s", sqlite3_errmsg(v_->known_words));
                sqlite3_reset (v_->update_learned_flag);
                return VARNAM_ERROR;
            }
        }

        sqlite3_bind_text  (v_->update_learned_flag, 1, pattern, -1, NULL);
        sqlite3_bind_int64 (v_->update_learned_flag, 2, word_id);

        rc = sqlite3_step (v_->update_learned_flag);
        if (rc != SQLITE_DONE) {
            set_last_error (handle, "Failed to learn pattern : %s", sqlite3_errmsg(v_->known_words));
            sqlite3_reset (v_->update_learned_flag);
            return VARNAM_ERROR;
        }
        sqlite3_reset (v_->update_learned_flag);
    }

    return VARNAM_SUCCESS;
}

int
vwt_get_word_id (varnam *handle, const char *word, sqlite3_int64 *word_id)
{
    int rc;

    assert (v_->known_words);

    if (v_->lastLearnedWord != NULL && strcmp (word, strbuf_to_s (v_->lastLearnedWord)) == 0) {
        *word_id = v_->lastLearnedWordId;
        return VARNAM_SUCCESS;
    }

    if (v_->get_word == NULL)
    {
        rc = sqlite3_prepare_v2( v_->known_words, "select id, word, confidence, learned_on from words where word = ?1 limit 1", -1, &v_->get_word, NULL );
        if (rc != SQLITE_OK) {
            set_last_error (handle, "Failed to get word : %s", sqlite3_errmsg(v_->known_words));
            sqlite3_reset (v_->get_word);
            return VARNAM_ERROR;
        }
    }

    *word_id = -1;
    sqlite3_bind_text (v_->get_word, 1, word, -1, NULL);

    rc = sqlite3_step (v_->get_word);
    if (rc == SQLITE_ROW) {
        *word_id = sqlite3_column_int64 (v_->get_word, 0);
    }
    else if (rc != SQLITE_DONE) {
        set_last_error (handle, "Failed to get word : %s", sqlite3_errmsg(v_->known_words));
        sqlite3_reset (v_->get_word);
        return VARNAM_ERROR;
    }

    sqlite3_reset (v_->get_word);
    return VARNAM_SUCCESS;
}

/* Learns the pattern. strbuf* is passed in because of memory optimizations.
 * See comments in the learn_prefixes() function */
static int
learn_pattern (varnam *handle, varray *tokens, const char *word, strbuf *pattern, bool is_prefix)
{
    int rc, i;
    sqlite3_int64 word_id;
    vtoken *token;

    rc = vwt_get_word_id (handle, word, &word_id);
    if (rc) return rc;

    strbuf_clear (pattern);
    for (i = 0; i < varray_length (tokens); i++)
    {
        token = varray_get (tokens, i);
        if (token->type != VARNAM_TOKEN_NON_JOINER && token->type != VARNAM_TOKEN_JOINER)
            strbuf_add (pattern, token->pattern);
    }

    rc = vwt_persist_pattern(handle, strbuf_to_s (pattern), word_id, is_prefix);
    if (rc)
        return rc;

    return VARNAM_SUCCESS;
}

static int
try_insert_new_word (varnam* handle, const char* word, int confidence, sqlite3_int64* new_word_id) {
    int rc;
    const char *sql = "insert or ignore into words (word, confidence, learned_on) values(trim(?1), ?2, strftime('%s', datetime(), 'localtime'));";

    *new_word_id = -1;

    if (v_->learn_word == NULL)
    {
        rc = sqlite3_prepare_v2( v_->known_words, sql, -1, &v_->learn_word, NULL );
        if (rc != SQLITE_OK) {
            set_last_error (handle, "Failed to learn word : %s", sqlite3_errmsg(v_->known_words));
            sqlite3_reset (v_->learn_word);
            return VARNAM_ERROR;
        }
    }

    sqlite3_bind_text (v_->learn_word, 1, word, -1, NULL);
    sqlite3_bind_int (v_->learn_word, 2, confidence);

    rc = sqlite3_step (v_->learn_word);
    if (rc != SQLITE_DONE) {
        set_last_error (handle, "Failed to learn word : %s", sqlite3_errmsg(v_->known_words));
        sqlite3_reset (v_->learn_word);
        return VARNAM_ERROR;
    }

    if (sqlite3_changes (v_->known_words) != 0) {
        *new_word_id = sqlite3_last_insert_rowid (v_->known_words);
    }

    sqlite3_reset (v_->learn_word);
    return VARNAM_SUCCESS;
}

static int
try_update_word_confidence (varnam* handle, const char* word, bool* updated) {
    int rc;
    *updated = false;

    if (v_->update_confidence == NULL)
    {
        rc = sqlite3_prepare_v2( v_->known_words,
                "update words set confidence = confidence + 1 where word = ?1;", -1,
                &v_->update_confidence, NULL );
        if (rc != SQLITE_OK) {
            set_last_error (handle, "Failed to learn word : %s", sqlite3_errmsg(v_->known_words));
            sqlite3_reset (v_->update_confidence);
            return VARNAM_ERROR;
        }
    }

    sqlite3_bind_text (v_->update_confidence, 1, word, -1, NULL);

    rc = sqlite3_step (v_->update_confidence);
    if (rc != SQLITE_DONE) {
        set_last_error (handle, "Failed to learn word : %s", sqlite3_errmsg(v_->known_words));
        sqlite3_reset (v_->update_confidence);
        return VARNAM_ERROR;
    }

    *updated = sqlite3_changes (v_->known_words);
    sqlite3_reset (v_->update_confidence);
    return VARNAM_SUCCESS;
}

static int
learn_word (varnam *handle, const char *word, int confidence, bool *new_word)
{
    int rc;
    bool confidence_updated;
    sqlite3_int64 new_word_id;

    assert (v_->known_words);

    if (v_->lastLearnedWord == NULL) {
        v_->lastLearnedWord = strbuf_init (20);
    }

    strbuf_clear (v_->lastLearnedWord);

    if (!v_->_config_mostly_learning_new_words) {
        rc = try_update_word_confidence (handle, word, &confidence_updated);
        if (rc) return rc;

        if (!confidence_updated) {
            rc = try_insert_new_word (handle, word, confidence, &new_word_id);
            if (rc) return rc;
            strbuf_add (v_->lastLearnedWord, word);
            v_->lastLearnedWordId = new_word_id;
        }
    }
    else {
        /* This assumes new words won't be already learned. So attempts insert first and fallback to update
         * confidence later */
        rc = try_insert_new_word (handle, word, confidence, &new_word_id);
        if (rc) return rc;

        if (new_word_id == -1) {
            rc = try_update_word_confidence (handle, word, &confidence_updated);
            if (rc) return rc;
        }
        else {
            strbuf_add (v_->lastLearnedWord, word);
            v_->lastLearnedWordId = new_word_id;
        }
    }

    /*varnam_log (handle, "Learned word %s", word);*/
    return VARNAM_SUCCESS;
}

/* Learns all the prefixes. This won't learn single tokens and the word itself
 * tokens_tmp - Is passed is for memory usage optimization. This function gets
 * called inside a cartesion product finder which means there will be a lot of
 * instances of array required. To optimize this, we pass in this array which
 * will be allocated from cartesian product finder */
static int
learn_prefixes(varnam *handle, varray *tokens, strbuf *pattern, bool word_already_learned)
{
    int i, rc, tokens_len = 0;
    vword *word;
    vtoken *token;
    bool new_word;

    varray *tokens_tmp = get_pooled_array (handle);
    for (i = 0; i < varray_length (tokens); i++)
    {
        token = varray_get (tokens, i);
        assert (token != NULL);

        varray_push (tokens_tmp, token);

        tokens_len = varray_length (tokens_tmp);
        /* We don't learn if it is only one token.
         * We don't learn the full word here because it would have already learned before this method is called */
        if (tokens_len > 1 && tokens_len != varray_length(tokens))
        {
            rc = resolve_tokens (handle, tokens_tmp, &word);
            if (rc) {
                return_array_to_pool (handle, tokens_tmp);
                return rc;
            }

            if (!word_already_learned)
            {
                rc = learn_word (handle, word->text, 1, &new_word);
                if (rc) {
                    return_array_to_pool (handle, tokens_tmp);
                    return rc;
                }
            }

            rc = learn_pattern (handle, tokens_tmp, word->text, pattern, true);
            if (rc) {
                return_array_to_pool (handle, tokens_tmp);
                return rc;
            }
        }
    }

    return_array_to_pool (handle, tokens_tmp);
    return VARNAM_SUCCESS;
}

void
print_tokens_array(varray *tokens)
{
    varray *tmp;
    vtoken *token;
    int i, j;

    printf("Tokens\n");

    for (i = 0; i < varray_length(tokens); i++)
    {
        tmp = varray_get (tokens, i);
        assert (tmp);
        printf("[");
        for (j = 0; j < varray_length(tmp); j++)
        {
            token = varray_get (tmp, j);
            assert (token);
            if (j + 1 == varray_length(tmp))
                printf("%d - '%s'", token->type, token->pattern);
            else
                printf("%d - '%s', ", token->type, token->pattern);
        }
        printf("]\n");
    }

}

/* This function learns all possibilities of writing the word and it's prefixes.
 * It finds cartesian product of the tokens passed in and process each product.
 * tokens will be a multidimensional array */
static int
learn_all_possibilities(varnam *handle, varray *tokens, const char *word)
{
    int rc, array_cnt, *offsets, i, last_array_offset, total = 0;
    varray *array, *tmp;
    strbuf *pattern;
    bool word_already_learned = false;

    array_cnt = varray_length (tokens);
    offsets = xmalloc(sizeof(int) * (size_t) array_cnt);

    for (i = 0; i < array_cnt; i++) offsets[i] = 0;

    array = get_pooled_array (handle);
    pattern = get_pooled_string (handle);

    for (;;)
    {
        varray_clear (array);
        for (i = 0; i < array_cnt; i++)
        {
            tmp = varray_get (tokens, i);
            assert (tmp);
            varray_push (array, varray_get (tmp, offsets[i]));
        }

        rc = learn_pattern (handle, array, word, pattern, false);
        if (rc)
            goto finished;

        rc = learn_prefixes (handle, array, pattern, word_already_learned);
        if (rc)
            goto finished;

        word_already_learned = true;
        if (++total == MAXIMUM_PATTERNS_TO_LEARN) {
            goto finished;
        }

        last_array_offset = array_cnt - 1;
        offsets[last_array_offset]++;

        while (offsets[last_array_offset] == varray_length ((varray*) varray_get (tokens, last_array_offset)))
        {
            offsets[last_array_offset] = 0;

            if (--last_array_offset < 0) goto finished;

            offsets[last_array_offset]++;
        }
    }

finished:
    xfree (offsets);
    return rc;
}

int
vwt_persist_possibilities(varnam *handle, varray *tokens, const char *word, int confidence)
{
    int rc;
    bool new_word;

    rc = learn_word (handle, word, confidence, &new_word);
    if (rc) return rc;

    rc = learn_all_possibilities (handle, tokens, word);
    if (rc) return rc;

    return VARNAM_SUCCESS;
}

int
vwt_get_best_match (varnam *handle, const char *input, varray *words)
{
    int rc;
    vword *word;
    const char *sql = "select word, confidence from words where rowid in "
                      "(SELECT word_id FROM patterns_content as pc where pc.pattern = lower(?1) and learned = 1 limit 5) "
                      "order by confidence desc";

    assert (handle);
    assert (words);

    if (v_->known_words == NULL)
        return VARNAM_SUCCESS;

    if (strlen(input) < MINIMUM_CHARACTER_LENGTH_FOR_SUGGESTION)
        return VARNAM_SUCCESS;

    if (v_->get_best_match == NULL)
    {
        rc = sqlite3_prepare_v2( v_->known_words, sql, -1, &v_->get_best_match, NULL );
        if (rc != SQLITE_OK) {
            set_last_error (handle, "Failed to get best matches : %s", sqlite3_errmsg(v_->known_words));
            sqlite3_reset (v_->get_best_match);
            return VARNAM_ERROR;
        }
    }

    sqlite3_bind_text (v_->get_best_match, 1, input, -1, NULL);

    for (;;)
    {
        rc = sqlite3_step (v_->get_best_match);
        if (rc == SQLITE_ROW)
        {
            word = get_pooled_word (handle,
                                    (const char*) sqlite3_column_text(v_->get_best_match, 0),
                                    (int) sqlite3_column_int(v_->get_best_match, 1));
            varray_push (words, word);
        }
        else if (rc == SQLITE_DONE)
        {
            break;
        }
        else
        {
            set_last_error (handle, "Failed to get best match : %s", sqlite3_errmsg(v_->known_words));
            sqlite3_reset (v_->get_best_match);
            return VARNAM_ERROR;
        }
    }

    sqlite3_clear_bindings (v_->get_best_match);
    sqlite3_reset (v_->get_best_match);
    return VARNAM_SUCCESS;
}

int
vwt_get_suggestions (varnam *handle, const char *input, varray *words)
{
    int rc;
    vword *word;
    const char *sql = "select word, confidence from words where rowid in "
                      "(SELECT distinct(word_id) FROM patterns_content as pc where pc.pattern > lower(?1) and pc.pattern <= lower(?1) || 'z' and learned = 1 limit 5) "
                      "order by confidence desc";

    assert (handle);
    assert (words);

    if (v_->known_words == NULL)
        return VARNAM_SUCCESS;

    if (strlen(input) < MINIMUM_CHARACTER_LENGTH_FOR_SUGGESTION)
        return VARNAM_SUCCESS;

    if (v_->get_suggestions == NULL)
    {
        rc = sqlite3_prepare_v2( v_->known_words, sql, -1, &v_->get_suggestions, NULL );
        if (rc != SQLITE_OK) {
            set_last_error (handle, "Failed to get suggestions : %s", sqlite3_errmsg(v_->known_words));
            sqlite3_reset (v_->get_suggestions);
            return VARNAM_ERROR;
        }
    }

    sqlite3_bind_text (v_->get_suggestions, 1, input, -1, NULL);

    for (;;)
    {
        rc = sqlite3_step (v_->get_suggestions);
        if (rc == SQLITE_ROW)
        {
            word = get_pooled_word (handle,
                                    (const char*) sqlite3_column_text(v_->get_suggestions, 0),
                                    (int) sqlite3_column_int(v_->get_suggestions, 1));
            if (!varray_exists (words, word, &word_equals))
            {
                varray_push (words, word);
            }
        }
        else if (rc == SQLITE_DONE)
        {
            break;
        }
        else
        {
            set_last_error (handle, "Failed to get suggestions : %s", sqlite3_errmsg(v_->known_words));
            sqlite3_reset (v_->get_suggestions);
            return VARNAM_ERROR;
        }
    }

    sqlite3_clear_bindings (v_->get_suggestions);
    sqlite3_reset (v_->get_suggestions);
    return VARNAM_SUCCESS;
}

static int
get_matches (varnam *handle, strbuf *lookup, varray *matches, bool *found)
{
    int rc;
    const char *sql = "select word from words where rowid in (select distinct(word_id) from patterns_content where pattern = ?1 limit 3);";
    strbuf *word;
    bool cleared = false;

    assert (v_->known_words);

    if (v_->get_matches_for_word == NULL)
    {
        rc = sqlite3_prepare_v2( v_->known_words, sql, -1, &v_->get_matches_for_word, NULL );
        if (rc != SQLITE_OK) {
            set_last_error (handle, "Failed to get matches : %s", sqlite3_errmsg(v_->known_words));
            sqlite3_reset (v_->get_matches_for_word);
            return VARNAM_ERROR;
        }
    }

    sqlite3_bind_text (v_->get_matches_for_word, 1, strbuf_to_s(lookup), -1, NULL);
    *found = false;
    for (;;)
    {
        rc = sqlite3_step (v_->get_matches_for_word);
        if (rc == SQLITE_ROW)
        {
            if (!cleared) {
                varray_clear (matches);
                cleared = true;
            }
            word = get_pooled_string (handle);
            strbuf_add (word, (const char*) sqlite3_column_text(v_->get_matches_for_word, 0));
            varray_push (matches, word);
            *found = true;
        }
        else if (rc == SQLITE_DONE)
        {
            break;
        }
        else
        {
            set_last_error (handle, "Failed to get matches : %s", sqlite3_errmsg(v_->known_words));
            sqlite3_reset (v_->get_matches_for_word);
            return VARNAM_ERROR;
        }
    }

    sqlite3_clear_bindings (v_->get_matches_for_word);
    sqlite3_reset (v_->get_matches_for_word);

    return VARNAM_SUCCESS;
}

static int
can_find_possible_matches (varnam *handle, strbuf *lookup, bool *possible)
{
    int rc;
    const char *sql = "SELECT distinct(word_id) FROM patterns_content as pc where pc.pattern > ?1 and pc.pattern <= ?1 || 'z'  limit 1;";

    assert (v_->known_words);

    if (v_->possible_to_find_matches == NULL)
    {
        rc = sqlite3_prepare_v2( v_->known_words, sql, -1, &v_->possible_to_find_matches, NULL );
        if (rc != SQLITE_OK) {
            set_last_error (handle, "Failed to check for possible matches : %s", sqlite3_errmsg(v_->known_words));
            sqlite3_reset (v_->possible_to_find_matches);
            return VARNAM_ERROR;
        }
    }

    sqlite3_bind_text (v_->possible_to_find_matches, 1, strbuf_to_s(lookup), -1, NULL);
    *possible = false;
    rc = sqlite3_step (v_->possible_to_find_matches);
    if (rc == SQLITE_ROW)
    {
        *possible = true;
    }
    else if (rc != SQLITE_DONE)
    {
        set_last_error (handle, "Failed to check for possible matches : %s", sqlite3_errmsg(v_->known_words));
        sqlite3_reset (v_->possible_to_find_matches);
        return VARNAM_ERROR;
    }

    sqlite3_clear_bindings (v_->possible_to_find_matches);
    sqlite3_reset (v_->possible_to_find_matches);

    return VARNAM_SUCCESS;
}

/* Gets the first element of each array in the specified multidimensional array */
static varray*
get_first_elements(varnam *handle, varray *source)
{
    int i, j;
    varray *a;
    varray *result = get_pooled_array (handle);

    for (i = 0; i < varray_length (source); i++)
    {
        a = varray_get (source, i);
        for (j = 0; j < varray_length (a); j++)
        {
            varray_push (result, varray_get (a, j));
            break;
        }
    }

    return result;
}

/* tokens will be a multidimensional array */
static void
add_tokens (varnam *handle, varray *tokens, varray *result, bool first_match)
{
    varray *tmp, *item;
    int j , k;

    tmp = get_first_elements (handle, tokens);
    if (first_match) {
        varray_push (result, tmp);
    }
    else
    {
        /* Append tokens to each element in the result */
        for (j = 0; j < varray_length (result); j++)
        {
            item = varray_get (result, j);
            for (k = 0; k < varray_length (tmp); k++)
            {
                varray_push (item, varray_get (tmp, k));
            }
        }
    }
}

static int
symbols_tokenize_add_to_result(varnam *handle, strbuf *lookup, varray *result)
{
    int rc;
    varray *tokens;

#ifdef _VARNAM_VERBOSE
    varnam_debug (handle, "Symbols tokenizing - %s", strbuf_to_s(lookup));
#endif

    tokens = get_pooled_array (handle);
    if (!strbuf_is_blank (lookup))
    {
        rc = vst_tokenize (handle, strbuf_to_s (lookup), VARNAM_TOKENIZER_PATTERN, VARNAM_MATCH_EXACT, tokens);
        if (rc) return rc;

        add_tokens (handle, tokens, result, false);
        strbuf_clear (lookup);
    }

    return_array_to_pool (handle, tokens);
    return VARNAM_SUCCESS;
}

/* Finds the longest possible match from the words table. Remaining string will be
   converted using symbols tokenization */
int
vwt_tokenize_pattern (varnam *handle, const char *pattern, varray *result)
{
    int rc, matchpos = 0, pos = 0, i;
    strbuf *lookup, *for_symbols_tokenization, *match;
    varray *matches;  /* contains strbuf* instances */
    varray *tokens;   /* Contains arrays that contains vtoken* instances */
    bool found = false, possible = false, first_match = true;
    const char *pc;

    varray_clear (result);

    if (v_->known_words == NULL)
        return VARNAM_SUCCESS;

    if (pattern == NULL || *pattern == '\0')
        return VARNAM_SUCCESS;

    lookup     = get_pooled_string (handle);
    matches    = get_pooled_array (handle);
    tokens     = get_pooled_array (handle);

    varnam_debug (handle, "Tokenizing '%s' with words tokenizer", pattern);

    pc = pattern;
    while (*pc != '\0')
    {
        strbuf_addc (lookup, *pc);
        ++pos; ++pc;

        rc = get_matches (handle, lookup, matches, &found);
        if (rc != VARNAM_SUCCESS)
            return rc;
        if (found) {
            matchpos = pos;
        }

        rc = can_find_possible_matches (handle, lookup, &possible);
        if (rc)
            return rc;
        if (possible)
            continue;
        else
            break;
    }

    /* At this point we will have the longest possible match. If nothing is available,
     * there is no words that matches the prefix. In that case, exiting early */
    if (varray_length (matches) == 0 && varray_length(result) == 0) {
        return VARNAM_SUCCESS;
    }

    for(i = 0; i < varray_length (matches); i++)
    {
        /* Tokenize the match */
        match = varray_get (matches, i);
        assert (match);
#ifdef _VARNAM_VERBOSE
        varnam_debug (handle, "Tokenizing longest prefix match - %s", strbuf_to_s (match));
#endif
        rc = vst_tokenize (handle, strbuf_to_s(match), VARNAM_TOKENIZER_VALUE, VARNAM_MATCH_EXACT, tokens);
        if (rc) return rc;

        add_tokens (handle, tokens, result, first_match);
        varray_clear (tokens);
    }
    pattern = pattern + matchpos;

    /* Remaining text will be tokenized literally */
    for_symbols_tokenization = get_pooled_string (handle);
    strbuf_add (for_symbols_tokenization, pattern);
    rc = symbols_tokenize_add_to_result (handle, for_symbols_tokenization, result);
    if (rc) return rc;

    strbuf_clear (lookup);
    strbuf_clear (for_symbols_tokenization);

    /* At this point, result will look like
     * [[t1,t2,t3], [t4,t5,t6]]*/

    return VARNAM_SUCCESS;
}

int
vwt_delete_word(varnam *handle, const char *word)
{
    int rc = 0;
    sqlite3_int64 word_id = 0;
    const char *pattern_sql = "delete from patterns_content where word_id = ?1;";
    const char *word_sql = "delete from words where id = ?1;";

    rc = vwt_get_word_id (handle, word, &word_id);
    if (rc != VARNAM_SUCCESS) {
        return rc;
    }

    if (v_->delete_pattern == NULL)
    {
        rc = sqlite3_prepare_v2( v_->known_words, pattern_sql, -1, &v_->delete_pattern, NULL );
        if (rc != SQLITE_OK) {
            set_last_error (handle, "Failed to delete word : %s", sqlite3_errmsg(v_->known_words));
            sqlite3_reset (v_->delete_pattern);
            return VARNAM_ERROR;
        }
    }

    if (v_->delete_word == NULL)
    {
        rc = sqlite3_prepare_v2( v_->known_words, word_sql, -1, &v_->delete_word, NULL );
        if (rc != SQLITE_OK) {
            set_last_error (handle, "Failed to delete word : %s", sqlite3_errmsg(v_->known_words));
            sqlite3_reset (v_->delete_word);
            return VARNAM_ERROR;
        }
    }

    rc = vwt_start_changes (handle);
    if (rc != VARNAM_SUCCESS) {
        return rc;
    }

    sqlite3_bind_int64 (v_->delete_pattern, 1, word_id);
    rc = sqlite3_step (v_->delete_pattern);
    if (rc != SQLITE_DONE) {
        set_last_error (handle, "Failed to delete pattern : %s", sqlite3_errmsg(v_->known_words));
        sqlite3_reset (v_->delete_pattern);
        vwt_discard_changes (handle);
        return VARNAM_ERROR;
    }
    sqlite3_reset (v_->delete_pattern);

    sqlite3_bind_int64 (v_->delete_word, 1, word_id);
    rc = sqlite3_step (v_->delete_word);
    if (rc != SQLITE_DONE) {
        set_last_error (handle, "Failed to delete word : %s", sqlite3_errmsg(v_->known_words));
        sqlite3_reset (v_->delete_word);
        vwt_discard_changes (handle);
        return VARNAM_ERROR;
    }
    sqlite3_reset (v_->delete_word);

    rc = vwt_end_changes (handle);
    if (rc != VARNAM_SUCCESS) {
        return rc;
    }

    return VARNAM_SUCCESS;
}

static int
get_learned_words_count(varnam *handle, int *words_count)
{
    int rc = 0;
    const char *sql = "select count(distinct(word_id)) from patterns_content where learned = 1;";

    if (v_->learned_words_count == NULL)
    {
        rc = sqlite3_prepare_v2( v_->known_words, sql, -1, &v_->learned_words_count, NULL );
        if (rc != SQLITE_OK) {
            set_last_error (handle, "Failed to get learned words count : %s", sqlite3_errmsg(v_->known_words));
            sqlite3_finalize (v_->learned_words_count);
            v_->learned_words_count = NULL;
            return VARNAM_ERROR;
        }
    }

    *words_count = 0;
    rc = sqlite3_step (v_->learned_words_count);
    if (rc == SQLITE_ROW) {
        *words_count = sqlite3_column_int (v_->learned_words_count, 0);
    }

    sqlite3_reset (v_->learned_words_count);

    return VARNAM_SUCCESS;
}

static int
get_all_words_count(varnam *handle, int *words_count)
{
    int rc = 0;
    sqlite3_stmt* stmt;

    rc = sqlite3_prepare_v2 (v_->known_words, "select count(id) from words;", -1, &stmt, NULL);
    if (rc != SQLITE_OK) {
        set_last_error (handle, "Failed to get all words count : %s", sqlite3_errmsg(v_->known_words));
        sqlite3_reset (stmt);
        sqlite3_finalize (stmt);
        return VARNAM_ERROR;
    }

    rc = sqlite3_step (stmt);
    if (rc == SQLITE_ROW) {
        *words_count = sqlite3_column_int (stmt, 0);
    }

    sqlite3_reset (stmt);
    sqlite3_finalize (stmt);

    return VARNAM_SUCCESS;
}

static int
get_all_patterns_count(varnam *handle, int *count)
{
    int rc = 0;
    sqlite3_stmt* stmt;

    rc = sqlite3_prepare_v2 (v_->known_words, "select count(*) from patterns_content;", -1, &stmt, NULL);
    if (rc != SQLITE_OK) {
        set_last_error (handle, "Failed to get all patterns count : %s", sqlite3_errmsg(v_->known_words));
        sqlite3_reset (stmt);
        sqlite3_finalize (stmt);
        return VARNAM_ERROR;
    }

    rc = sqlite3_step (stmt);
    if (rc == SQLITE_ROW) {
        *count = sqlite3_column_int (stmt, 0);
    }

    sqlite3_reset (stmt);
    sqlite3_finalize (stmt);

    return VARNAM_SUCCESS;
}

static int
full_export_words(varnam* handle, int words_per_file, int total_words, const char* out_dir,
        void (*callback)(int, int, const char *), int* out_words_processed)
{
    int rc, id, confidence, words_written = 0, file_index = 0;
    strbuf* path;
    const char* word;
    FILE* fp = NULL;
    sqlite3_stmt* stmt = NULL;
    const char* sql = "select id, word, confidence from words;";

    rc = sqlite3_prepare_v2 (v_->known_words, sql, -1, &stmt, NULL);
    if (rc != SQLITE_OK) {
        set_last_error (handle, "Failed to export all words : %s", sqlite3_errmsg(v_->known_words));
        sqlite3_finalize (stmt);
        return VARNAM_ERROR;
    }

    for (;;)
    {
        rc = sqlite3_step (stmt);
        if (rc == SQLITE_ROW)
        {
            if (fp == NULL) {
                path = get_pooled_string (handle);
                strbuf_addf (path, "%s/%d.words.txt",  out_dir, file_index++);
                fp = fopen (strbuf_to_s (path), "w");
                if (fp == NULL) {
                    set_last_error (handle, "Failed to open : %s", strbuf_to_s (path));
                    sqlite3_finalize (stmt);
                    return VARNAM_ERROR;
                }

                /* First line will be the file type identifier */
                fprintf (fp, "%s\n", VARNAM_WORDS_EXPORT_METADATA);
            }

            id = (int) sqlite3_column_int (stmt, 0);
            word = (const char*) sqlite3_column_text (stmt, 1);
            confidence = (int) sqlite3_column_int (stmt, 2);
            fprintf (fp, "%d %s %d\n", id, word, confidence);
            *out_words_processed = *out_words_processed + 1;

            if (callback != NULL) {
                callback (total_words, *out_words_processed, word);
            }

            if (++words_written == words_per_file) {
                words_written = 0;
                fclose (fp);
                fp = NULL;
            }
        }
        else if (rc == SQLITE_DONE) {
            break;
        }
        else {
            set_last_error (handle, "Failed to get all words : %s", sqlite3_errmsg(v_->known_words));
            sqlite3_finalize (stmt);
            return VARNAM_ERROR;
        }
    }

    sqlite3_finalize (stmt);
    return VARNAM_SUCCESS;
}

static int
full_export_patterns(varnam* handle, int words_per_file, int total_words, const char* out_dir,
        void (*callback)(int, int, const char *), int* out_words_processed)
{
    int rc, word_id, learned, words_written = 0, file_index = 0;
    strbuf* path;
    const char* pattern;
    FILE* fp = NULL;
    sqlite3_stmt* stmt = NULL;
    const char* sql = "select word_id, pattern, learned from patterns_content;";

    rc = sqlite3_prepare_v2 (v_->known_words, sql, -1, &stmt, NULL);
    if (rc != SQLITE_OK) {
        set_last_error (handle, "Failed to export all patterns : %s", sqlite3_errmsg(v_->known_words));
        sqlite3_finalize (stmt);
        return VARNAM_ERROR;
    }

    for (;;)
    {
        rc = sqlite3_step (stmt);
        if (rc == SQLITE_ROW)
        {
            if (fp == NULL) {
                path = get_pooled_string (handle);
                strbuf_addf (path, "%s/%d.patterns.txt",  out_dir, file_index++);
                fp = fopen (strbuf_to_s (path), "w");
                if (fp == NULL) {
                    set_last_error (handle, "Failed to open : %s", strbuf_to_s (path));
                    sqlite3_finalize (stmt);
                    return VARNAM_ERROR;
                }

                /* First line will be the file type identifier */
                fprintf (fp, "%s\n", VARNAM_PATTERNS_EXPORT_METADATA);
            }

            word_id = (int) sqlite3_column_int (stmt, 0);
            pattern = (const char*) sqlite3_column_text (stmt, 1);
            learned = (int) sqlite3_column_int (stmt, 2);
            fprintf (fp, "%d %s %d\n", word_id, pattern, learned);
            *out_words_processed = *out_words_processed + 1;

            if (callback != NULL) {
                callback (total_words, *out_words_processed, pattern);
            }

            if (++words_written == words_per_file) {
                words_written = 0;
                fclose (fp);
                fp = NULL;
            }
        }
        else if (rc == SQLITE_DONE) {
            break;
        }
        else {
            set_last_error (handle, "Failed to get all patterns : %s", sqlite3_errmsg(v_->known_words));
            sqlite3_finalize (stmt);
            return VARNAM_ERROR;
        }
    }

    sqlite3_finalize (stmt);
    return VARNAM_SUCCESS;
}

int
vwt_full_export(varnam* handle, int words_per_file, const char* out_dir,
    void (*callback)(int, int, const char *))
{
    int rc, processed = 0, total = 0, tmp = 0;

    rc = get_all_words_count (handle, &tmp);
    if (rc) return rc;

    total = tmp;

    rc = get_all_patterns_count (handle, &tmp);
    if (rc) return rc;

    total = total + tmp;

    rc = full_export_words (handle, words_per_file, total, out_dir, callback, &processed);
    if (rc) return rc;

    rc = full_export_patterns (handle, words_per_file, total, out_dir, callback, &processed);
    return rc;
}

int
vwt_export_words(varnam* handle, int words_per_file, const char* out_dir,
    void (*callback)(int, int, const char *))
{
    int rc = 0, words_written = 0, file_index = 0, total_words = 0, total_processed = 0;
    const char* sql = "select word, confidence from words where id in (select distinct(word_id) from patterns_content where learned = 1) order by confidence desc;";
    FILE* fp = NULL;
    strbuf* path = NULL;
    const char *current_word = "";

    if (v_->export_words == NULL)
    {
        rc = sqlite3_prepare_v2( v_->known_words, sql, -1, &v_->export_words, NULL );
        if (rc != SQLITE_OK) {
            set_last_error (handle, "Failed to export words : %s", sqlite3_errmsg(v_->known_words));
            sqlite3_reset (v_->export_words);
            return VARNAM_ERROR;
        }
    }

    assert (v_->export_words);
    assert (words_per_file > 0);

    rc = get_learned_words_count (handle, &total_words);
    if (rc != VARNAM_SUCCESS) {
        return rc;
    }

    if (total_words <= 0) {
        sqlite3_reset (v_->export_words);
        return VARNAM_SUCCESS;
    }

    for (;;)
    {
        rc = sqlite3_step (v_->export_words);
        if (rc == SQLITE_ROW)
        {
            if (fp == NULL) {
                path = get_pooled_string (handle);
                strbuf_addf (path, "%s/%d.txt",  out_dir, file_index++);
                fp = fopen (strbuf_to_s (path), "w");
                if (fp == NULL) {
                    set_last_error (handle, "Failed to open : %s", strbuf_to_s (path));
                    sqlite3_reset (v_->export_words);
                    return VARNAM_ERROR;
                }
            }

            current_word = (const char*) sqlite3_column_text (v_->export_words, 0);
            fprintf (fp, "%s %d\n", current_word, (int) sqlite3_column_int (v_->export_words, 1));
            ++total_processed;

            if (callback != NULL) {
                callback (total_words, total_processed, current_word);
            }

            if (++words_written == words_per_file) {
                words_written = 0;
                fclose (fp);
                fp = NULL;
            }
        }
        else if (rc == SQLITE_DONE) {
            break;
        }
        else {
            set_last_error (handle, "Failed to get matches : %s", sqlite3_errmsg(v_->known_words));
            sqlite3_reset (v_->export_words);
            return VARNAM_ERROR;
        }
    }

    if (fp != NULL) {
        fclose (fp);
        fp = NULL;
    }
    sqlite3_reset (v_->export_words);

    return VARNAM_SUCCESS;
}

#define _IMPORT_BUF_SIZE 1000
int
vwt_import_words (varnam* handle, FILE* file, void (*onfailure)(const char* line))
{
    int rc;
    char buffer[_IMPORT_BUF_SIZE];
    strbuf* line; strbuf* id; strbuf* word; strbuf* confidence;
    varray* parts;
    sqlite3_stmt* stmt = NULL;
    const char* sql = "insert into words (id, word, confidence, learned_on) values (?1, ?2, ?3, strftime('%s', datetime(), 'localtime'));";

    rc = sqlite3_prepare_v2 (v_->known_words, sql, -1, &stmt, NULL);
    if (rc != SQLITE_OK) {
        set_last_error (handle, "Failed to import words : %s", sqlite3_errmsg(v_->known_words));
        sqlite3_finalize (stmt);
        return VARNAM_ERROR;
    }

    line = get_pooled_string (handle);
    while (fgets(buffer, _IMPORT_BUF_SIZE, file))
    {
        strbuf_clear (line);
        strbuf_add (line, trimwhitespace (buffer));
        parts = strbuf_split (line, handle, ' ');

        if (varray_length (parts) != 3) {
            if (onfailure != NULL)
                onfailure (buffer);
            continue;
        }

        id = varray_get (parts, 0);
        word = varray_get (parts, 1);
        confidence = varray_get (parts, 2);

        sqlite3_bind_text (stmt, 1, strbuf_to_s (id), -1, NULL);
        sqlite3_bind_text (stmt, 2, strbuf_to_s (word), -1, NULL);
        sqlite3_bind_text (stmt, 3, strbuf_to_s (confidence), -1, NULL);

        rc = sqlite3_step (stmt);
        if (rc != SQLITE_DONE) {
            set_last_error (handle, "Failed to import word: %s. %s\n", word, sqlite3_errmsg(v_->known_words));
            sqlite3_reset (stmt);
            sqlite3_finalize (stmt);
            return VARNAM_ERROR;
        }

        sqlite3_clear_bindings (stmt);
        sqlite3_reset (stmt);

        return_string_to_pool (handle, id);
        return_string_to_pool (handle, word);
        return_string_to_pool (handle, confidence);
    }

    sqlite3_reset (stmt);
    sqlite3_finalize (stmt);
    return VARNAM_SUCCESS;
}

int
vwt_import_patterns (varnam* handle, FILE* file, void (*onfailure)(const char* line))
{
    int rc;
    char buffer[_IMPORT_BUF_SIZE];
    strbuf* line; strbuf* id; strbuf* pattern; strbuf* learned;
    varray* parts;
    sqlite3_stmt* stmt = NULL;
    const char* sql = "insert into patterns_content (word_id, pattern, learned) values (?1, ?2, ?3);";

    rc = sqlite3_prepare_v2 (v_->known_words, sql, -1, &stmt, NULL);
    if (rc != SQLITE_OK) {
        set_last_error (handle, "Failed to import patterns : %s", sqlite3_errmsg(v_->known_words));
        sqlite3_finalize (stmt);
        return VARNAM_ERROR;
    }

    line = get_pooled_string (handle);
    while (fgets(buffer, _IMPORT_BUF_SIZE, file))
    {
        strbuf_clear (line);
        strbuf_add (line, trimwhitespace (buffer));
        parts = strbuf_split (line, handle, ' ');

        if (varray_length (parts) != 3) {
            if (onfailure != NULL)
                onfailure (buffer);
            continue;
        }

        id = varray_get (parts, 0);
        pattern = varray_get (parts, 1);
        learned = varray_get (parts, 2);

        sqlite3_bind_text (stmt, 1, strbuf_to_s (id), -1, NULL);
        sqlite3_bind_text (stmt, 2, strbuf_to_s (pattern), -1, NULL);
        sqlite3_bind_text (stmt, 3, strbuf_to_s (learned), -1, NULL);

        rc = sqlite3_step (stmt);
        if (rc != SQLITE_DONE) {
            set_last_error (handle, "Failed to import pattern: %s. %s\n", pattern, sqlite3_errmsg(v_->known_words));
            sqlite3_reset (stmt);
            sqlite3_finalize (stmt);
            return VARNAM_ERROR;
        }

        sqlite3_clear_bindings (stmt);
        sqlite3_reset (stmt);

        return_string_to_pool (handle, id);
        return_string_to_pool (handle, pattern);
        return_string_to_pool (handle, learned);
    }

    sqlite3_reset (stmt);
    sqlite3_finalize (stmt);
    return VARNAM_SUCCESS;
}



/* int */
/* vwt_tokenize_pattern (varnam *handle, const char *pattern, varray *result) */
/* { */
/*     int rc, matchpos = 0, pos = 0, i; */
/*     strbuf *lookup, *for_symbols_tokenization, *match; */
/*     varray *matches;  /\* contains strbuf* instances *\/ */
/*     varray *tokens;   /\* Contains arrays that contains vtoken* instances *\/ */
/*     bool found = false, possible = false, first_match = true; */
/*     const char *pc; */

/*     varray_clear (result); */

/*     if (v_->known_words == NULL) */
/*         return VARNAM_SUCCESS; */

/*     if (pattern == NULL || *pattern == '\0') */
/*         return VARNAM_SUCCESS; */

/*     lookup                   = get_pooled_string (handle); */
/*     for_symbols_tokenization = get_pooled_string (handle); */
/*     matches                  = get_pooled_array (handle); */
/*     tokens                   = get_pooled_array (handle); */

/*     varnam_debug (handle, "Tokenizing '%s' with words tokenizer", pattern); */

/*     pc = pattern; */
/*     while (*pc != '\0') */
/*     { */
/*         strbuf_addc (lookup, *pc); */
/*         ++pos; ++pc; */

/*         rc = get_matches (handle, lookup, matches, &found); */
/*         if (rc != VARNAM_SUCCESS) */
/*             return rc; */
/*         if (found) { */
/*             matchpos = pos; */
/*         } */

/*         rc = can_find_possible_matches (handle, lookup, &possible); */
/*         if (rc) */
/*             return rc; */
/*         if (possible) */
/*             continue; */

/*         /\* At this point we will have the longest possible match. If nothing is available, */
/*          * there is no words that matches the prefix. In that case, exiting early *\/ */
/*         if (varray_length (matches) == 0 && varray_length(result) == 0) { */
/*             return VARNAM_SUCCESS; */
/*         } */

/*         if (varray_length (matches) > 0) */
/*         { */
/*             rc = symbols_tokenize_add_to_result (handle, for_symbols_tokenization, result); */
/*             if (rc) return rc; */
/*             strbuf_clear (for_symbols_tokenization); */

/*             for(i = 0; i < varray_length (matches); i++) */
/*             { */
/*                 /\* Tokenize the match *\/ */
/*                 match = varray_get (matches, i); */
/*                 assert (match); */
/* #ifdef _VARNAM_VERBOSE */
/*                 varnam_debug (handle, "Tokenizing longest prefix match - %s", strbuf_to_s (match)); */
/* #endif */
/*                 rc = vst_tokenize (handle, strbuf_to_s(match), VARNAM_TOKENIZER_VALUE, VARNAM_MATCH_EXACT, tokens); */
/*                 if (rc) return rc; */

/*                 add_tokens (handle, tokens, result, first_match); */
/*                 varray_clear (tokens); */
/*             } */
/*             first_match = false; */
/*             varray_clear (matches); */
/*         } */
/*         else */
/*         { */
/*             matchpos = 1; */
/*             /\* Remembering the failed portion as we will be using this later to do the symbols */
/*              * tokenization *\/ */
/*             strbuf_addc (for_symbols_tokenization, strbuf_to_s(lookup)[0]); */
/* #ifdef _VARNAM_VERBOSE */
/*             varnam_debug (handle, "Failed to find match. Lookup = %s. For symbols tokenization = %s", strbuf_to_s (lookup), strbuf_to_s(for_symbols_tokenization)); */
/* #endif */
/*         } */
/*         pattern = pattern + matchpos; */
/*         pc = pattern; */
/*         pos = 0; */
/*         matchpos = 0; */
/*         strbuf_clear (lookup); */
/*     } */

/*     /\* If we still have text remaining in this buffer, means that the tokenization ended without */
/*      * getting any more matches after it failed last time to find a match */
/*      * if it would have got a match, it would have tokenized the items in the buffer. */
/*      * */
/*      * Loop above might have completed before it records failed characters for symbols tokenization. */
/*      * So adding remaining items in the lookup for tokenization *\/ */
/*     strbuf_add (for_symbols_tokenization, strbuf_to_s (lookup)); */
/*     rc = symbols_tokenize_add_to_result (handle, for_symbols_tokenization, result); */
/*     if (rc) return rc; */

/*     strbuf_clear (lookup); */
/*     strbuf_clear (for_symbols_tokenization); */

/*     /\* At this point, result will look like */
/*      * [[t1,t2,t3], [t4,t5,t6]]*\/ */

/*     return VARNAM_SUCCESS; */
/* } */
