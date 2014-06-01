/* Tests word export
 *
 * Copyright (C) Navaneeth.K.N
 *
 * This is part of libvarnam. See LICENSE.txt for the license
 */

#include <stdio.h>
#include <string.h>
#include <math.h>
#include "../varnam.h"
#include <check.h>
#include "testcases.h"

static const char* word1 =  "മലയാളം";
static const char* word2 = "വർണം";
static const char* word3 = "തമിഴ്";

static void
setup_data()
{
    int rc;
    char* unique_filename = NULL;
   
    const char *filename = "../schemes/ml.vst";
    if (!file_exist (filename)) {
        ck_abort_msg ("../schemes/ml.vst is not available");
    }
    reinitialize_varnam_instance (filename);

    unique_filename = get_unique_filename();
    rc = varnam_config (varnam_instance, VARNAM_CONFIG_ENABLE_SUGGESTIONS, unique_filename);
    assert_success (rc);

    rc = varnam_learn (varnam_instance, word1);
    assert_success (rc);

    rc = varnam_learn (varnam_instance, word2);
    assert_success (rc);

    rc = varnam_learn (varnam_instance, word3);
    assert_success (rc);

    free (unique_filename);
}

START_TEST (varnam_export)
{
    int rc;
    int confidence;
    FILE* fp;
    char buffer[100];

    rc = varnam_export_words (varnam_instance, 2, "output/", VARNAM_EXPORT_WORDS, NULL);
    assert_success (rc);

    fp = fopen ("output/0.txt", "r");
    if (fp == NULL) {
        ck_abort_msg ("Error opening output/0.txt");
    }

    fscanf (fp, "%s %d", buffer, &confidence);
    ck_assert_str_eq (buffer, word1);

    fscanf (fp, "%s %d", buffer, &confidence);
    ck_assert_str_eq (buffer, word2);

    if (fscanf (fp, "%s %d", buffer, &confidence) == 2) {
        ck_abort_msg ("0.txt has more than 2 lines");
    }

    fclose (fp);

    fp = fopen ("output/1.txt", "r");
    if (fp == NULL) {
        ck_abort_msg ("Error opening output/1.txt");
    }

    fscanf (fp, "%s %d", buffer, &confidence);
    ck_assert_str_eq (buffer, word3);

    if (fscanf (fp, "%s %d", buffer, &confidence) == 1) {
        ck_abort_msg ("1.txt has more than 1 lines");
    }

    fclose (fp);
}
END_TEST

START_TEST (varnam_export_full)
{
    int rc, pcnt, wcnt, i;
    float filecnt;
    strbuf* f; strbuf* error;

    f = strbuf_init (20);
    pcnt = execute_query_int (varnam_instance->internal->known_words, "select count(*) from patterns_content;");
    wcnt = execute_query_int (varnam_instance->internal->known_words, "select count(*) from words;");

    rc = varnam_export_words (varnam_instance, 2, "output/", VARNAM_EXPORT_FULL, NULL);
    assert_success (rc);

    filecnt = pcnt / 2;
    for (i = 0; i < (int) ceil (filecnt); i++) {
        strbuf_clear (f);
        strbuf_addf (f, "output/%d.patterns.txt", i);
        if (!file_exist (strbuf_to_s (f))) {
            error = strbuf_init (10);
            strbuf_addf (error, "Failed to find file: %s\n", strbuf_to_s (f));
            ck_abort_msg (strbuf_to_s (error));
        }
    }

    filecnt = wcnt / 2;
    for (i = 0; i < (int) ceil (filecnt); i++) {
        strbuf_clear (f);
        strbuf_addf (f, "output/%d.words.txt", i);
        if (!file_exist (strbuf_to_s (f))) {
            error = strbuf_init (10);
            strbuf_addf (error, "Failed to find file: %s\n", strbuf_to_s (f));
            ck_abort_msg (strbuf_to_s (error));
        }
    }

    strbuf_destroy (f);
}
END_TEST

START_TEST (varnam_export_args_check)
{
    int rc;

    rc = varnam_export_words (NULL, 2, "output/", VARNAM_EXPORT_FULL, NULL);
    ck_assert_int_eq (rc, VARNAM_ARGS_ERROR);

    rc = varnam_export_words (varnam_instance, 0, "output/", VARNAM_EXPORT_FULL, NULL);
    ck_assert_int_eq (rc, VARNAM_ARGS_ERROR);
}
END_TEST

START_TEST (varnam_import_learnings)
{
    int rc, pcnt, wcnt;

    execute_query (varnam_instance->internal->known_words, "delete from words;");
    execute_query (varnam_instance->internal->known_words, "delete from patterns_content;");

    pcnt = execute_query_int (varnam_instance->internal->known_words, "select count(*) from patterns_content;");
    wcnt = execute_query_int (varnam_instance->internal->known_words, "select count(*) from words;");
    ck_assert_int_eq (0, pcnt);
    ck_assert_int_eq (0, wcnt);

    rc = varnam_import_learnings_from_file (varnam_instance, "output/0.words.txt", NULL);
    assert_success (rc);
    rc = varnam_import_learnings_from_file (varnam_instance, "output/0.patterns.txt", NULL);
    assert_success (rc);

    pcnt = execute_query_int (varnam_instance->internal->known_words, "select count(*) from patterns_content;");
    wcnt = execute_query_int (varnam_instance->internal->known_words, "select count(*) from words;");
    ck_assert (pcnt != 0);
    ck_assert (wcnt != 0);
}
END_TEST

START_TEST (varnam_import_learnings_invalid_file)
{
    int rc;
    rc = varnam_import_learnings_from_file (varnam_instance, "output/invalidfile.txt", NULL);
    ck_assert_int_eq (rc, VARNAM_ERROR);
}
END_TEST

START_TEST (varnam_import_learnings_wrong_filetype)
{
    FILE* fp;
    int rc;

    fp = fopen ("output/wrong_file_type.txt", "w");
    ck_assert (fp != NULL);

    fprintf (fp, "%s\n", "Wrong filetype");
    fclose (fp);

    rc = varnam_import_learnings_from_file (varnam_instance, "output/wrong_file_type.txt", NULL);
    ck_assert_int_eq (rc, VARNAM_ERROR);
    ck_assert_str_eq ("Couldn't read file 'output/wrong_file_type.txt'. Unknown file type", varnam_get_last_error (varnam_instance));
}
END_TEST

static int cbinvoked = 0;

static void
import_failed_cb(const char* word)
{
    cbinvoked = 1;
    ck_assert_str_eq ("1 df", word);
}

START_TEST (varnam_import_learnings_failure_callback)
{
    FILE* fp;
    int rc;

    fp = fopen ("output/improper_csv.txt", "w");
    ck_assert (fp != NULL);

    fprintf (fp, "%s\n%s\n", VARNAM_WORDS_EXPORT_METADATA, "1 df");
    fclose (fp);

    rc = varnam_import_learnings_from_file (varnam_instance, "output/improper_csv.txt", import_failed_cb);
    ck_assert_int_eq (rc, VARNAM_SUCCESS);
    ck_assert_int_eq (1, cbinvoked);
}
END_TEST

START_TEST (varnam_import_learnings_invalid_args)
{
    int rc;
    rc = varnam_import_learnings_from_file (NULL, "", NULL);
    ck_assert_int_eq (rc, VARNAM_ARGS_ERROR);
    rc = varnam_import_learnings_from_file (varnam_instance, NULL, NULL);
    ck_assert_int_eq (rc, VARNAM_ARGS_ERROR);
}
END_TEST

TCase* get_export_tests()
{
    TCase* tcase = tcase_create("export");
    tcase_add_checked_fixture (tcase, setup, teardown);
    tcase_add_checked_fixture (tcase, setup_data, NULL);
    tcase_add_test (tcase, varnam_export);
    tcase_add_test (tcase, varnam_export_full);
    tcase_add_test (tcase, varnam_export_args_check);
    tcase_add_test (tcase, varnam_import_learnings);
    tcase_add_test (tcase, varnam_import_learnings_invalid_file);
    tcase_add_test (tcase, varnam_import_learnings_wrong_filetype);
    tcase_add_test (tcase, varnam_import_learnings_failure_callback);
    tcase_add_test (tcase, varnam_import_learnings_invalid_args);
    return tcase;
}
