#include "otl-chaining.h"
static void deleteRule(otl_chaining_rule *rule) {
	if (rule && rule->match && rule->matchCount) {
		for (uint16_t k = 0; k < rule->matchCount; k++) {
			caryll_delete_coverage(rule->match[k]);
		}
	}
	if (rule && rule->apply) free(rule->apply);
	if (rule) free(rule);
}
void delete_otl_chaining_subtable(otl_subtable *_subtable) {
	if (_subtable) {
		subtable_chaining *subtable = &(_subtable->chaining);
		if (subtable->rules) {
			for (uint16_t j = 0; j < subtable->rulesCount; j++) {
				deleteRule(subtable->rules[j]);
			}
			free(subtable->rules);
		}
		if (subtable->bc) {
			if (subtable->bc->glyphs) free(subtable->bc->glyphs);
			if (subtable->bc->classes) free(subtable->bc->classes);
		}
		if (subtable->ic) {
			if (subtable->ic->glyphs) free(subtable->ic->glyphs);
			if (subtable->ic->classes) free(subtable->ic->classes);
		}
		if (subtable->fc) {
			if (subtable->fc->glyphs) free(subtable->fc->glyphs);
			if (subtable->fc->classes) free(subtable->fc->classes);
		}
		free(_subtable);
	}
}
void caryll_delete_chaining(otl_lookup *lookup) {
	if (lookup) {
		if (lookup->subtables)
			for (uint16_t j = 0; j < lookup->subtableCount; j++)
				delete_otl_chaining_subtable(lookup->subtables[j]);
		free(lookup);
	}
}

static INLINE void reverseBacktracks(otl_chaining_rule *rule) {
	if (rule->inputBegins > 0) {
		uint16_t start = 0;
		uint16_t end = rule->inputBegins - 1;
		while (end > start) {
			otl_coverage *tmp = rule->match[start];
			rule->match[start] = rule->match[end];
			rule->match[end] = tmp;
			end--, start++;
		}
	}
}

typedef struct {
	otl_classdef *bc;
	otl_classdef *ic;
	otl_classdef *fc;
} classdefs;

otl_coverage *singleCoverage(font_file_pointer data, uint32_t tableLength, uint16_t gid, uint32_t _offset,
                             uint16_t kind, void *userdata) {
	otl_coverage *cov;
	NEW(cov);
	cov->numGlyphs = 1;
	NEW(cov->glyphs);
	cov->glyphs[0].gid = gid;
	cov->glyphs[0].name = NULL;
	return cov;
}
otl_coverage *classCoverage(font_file_pointer data, uint32_t tableLength, uint16_t cls, uint32_t _offset, uint16_t kind,
                            void *_classdefs) {
	classdefs *defs = (classdefs *)_classdefs;
	otl_classdef *cd = (kind == 1 ? defs->bc : kind == 2 ? defs->ic : defs->fc);
	otl_coverage *cov;
	NEW(cov);
	cov->numGlyphs = 0;
	cov->glyphs = NULL;
	uint16_t count = 0;
	for (uint16_t j = 0; j < cd->numGlyphs; j++)
		if (cd->classes[j] == cls) count++;
	if (!count) return cov;
	cov->numGlyphs = count;
	NEW_N(cov->glyphs, count);
	uint16_t jj = 0;
	for (uint16_t j = 0; j < cd->numGlyphs; j++)
		if (cd->classes[j] == cls) { cov->glyphs[jj++] = cd->glyphs[j]; }
	return cov;
}
otl_coverage *format3Coverage(font_file_pointer data, uint32_t tableLength, uint16_t shift, uint32_t _offset,
                              uint16_t kind, void *userdata) {
	return caryll_read_coverage(data, tableLength, _offset + shift - 2);
}
typedef otl_coverage *(*CoverageReaderHandler)(font_file_pointer, uint32_t, uint16_t, uint32_t, uint16_t, void *);

otl_chaining_rule *GeneralReadContextualRule(font_file_pointer data, uint32_t tableLength, uint32_t offset,
                                             uint16_t startGID, bool minusOne, CoverageReaderHandler fn,
                                             void *userdata) {
	otl_chaining_rule *rule;
	NEW(rule);
	rule->match = NULL;
	rule->apply = NULL;

	checkLength(offset + 4);
	uint16_t nInput = read_16u(data + offset);
	uint16_t nApply = read_16u(data + offset + 2);
	checkLength(offset + 4 + 2 * nInput + 4 * nApply);

	rule->matchCount = nInput;
	rule->inputBegins = 0;
	rule->inputEnds = nInput;

	NEW_N(rule->match, rule->matchCount);
	uint16_t jj = 0;
	if (minusOne) { rule->match[jj++] = fn(data, tableLength, startGID, offset, 2, userdata); }
	for (uint16_t j = 0; j < nInput - (minusOne ? 1 : 0); j++) {
		uint32_t gid = read_16u(data + offset + 4 + j * 2);
		rule->match[jj++] = fn(data, tableLength, gid, offset, 2, userdata);
	}
	rule->applyCount = nApply;
	NEW_N(rule->apply, rule->applyCount);
	for (uint16_t j = 0; j < nApply; j++) {
		rule->apply[j].index =
		    rule->inputBegins + read_16u(data + offset + 4 + 2 * (rule->matchCount - (minusOne ? 1 : 0)) + j * 4);
		rule->apply[j].lookupIndex =
		    read_16u(data + offset + 4 + 2 * (rule->matchCount - (minusOne ? 1 : 0)) + j * 4 + 2);
		rule->apply[j].lookupName = NULL;
	}
	reverseBacktracks(rule);
	return rule;

FAIL:
	DELETE(deleteRule, rule);
	return NULL;
}

otl_subtable *caryll_read_contextual(font_file_pointer data, uint32_t tableLength, uint32_t offset) {
	uint16_t format = 0;
	otl_subtable *_subtable;
	NEW(_subtable);
	subtable_chaining *subtable = &(_subtable->chaining);
	subtable->rulesCount = 0;
	subtable->classified = false;
	subtable->bc = NULL;
	subtable->ic = NULL;
	subtable->fc = NULL;
	subtable->rules = NULL;
	checkLength(offset + 2);
	format = read_16u(data + offset);
	if (format == 1) {
		checkLength(offset + 6);
		otl_coverage *firstCoverage = caryll_read_coverage(data, tableLength, offset + read_16u(data + offset + 2));
		uint16_t chainSubRuleSetCount = read_16u(data + offset + 4);
		if (chainSubRuleSetCount != firstCoverage->numGlyphs) goto FAIL;
		checkLength(offset + 6 + 2 * chainSubRuleSetCount);
		uint16_t totalRules = 0;
		for (uint16_t j = 0; j < chainSubRuleSetCount; j++) {
			uint32_t srsOffset = offset + read_16u(data + offset + 6 + j * 2);
			checkLength(srsOffset + 2);
			totalRules += read_16u(data + srsOffset);
			checkLength(srsOffset + 2 + 2 * read_16u(data + srsOffset));
		}
		subtable->rulesCount = totalRules;
		NEW_N(subtable->rules, totalRules);
		uint16_t jj = 0;
		for (uint16_t j = 0; j < chainSubRuleSetCount; j++) {
			uint32_t srsOffset = offset + read_16u(data + offset + 6 + j * 2);
			uint16_t srsCount = read_16u(data + srsOffset);
			for (uint16_t k = 0; k < srsCount; k++) {
				uint32_t srOffset = srsOffset + read_16u(data + srsOffset + 2 + k * 2);
				subtable->rules[jj] = GeneralReadContextualRule(
				    data, tableLength, srOffset, firstCoverage->glyphs[j].gid, true, singleCoverage, NULL);
				jj += 1;
			}
		}
		caryll_delete_coverage(firstCoverage);
		return _subtable;
	} else if (format == 2) {
		checkLength(offset + 8);
		classdefs *cds;
		NEW(cds);
		cds->bc = NULL;
		cds->ic = caryll_raad_classdef(data, tableLength, offset + read_16u(data + offset + 4));
		cds->fc = NULL;
		uint16_t chainSubClassSetCnt = read_16u(data + offset + 6);
		checkLength(offset + 12 + 2 * chainSubClassSetCnt);
		uint16_t totalRules = 0;
		for (uint16_t j = 0; j < chainSubClassSetCnt; j++) {
			uint16_t srcOffset = read_16u(data + offset + 8 + j * 2);
			if (srcOffset) { totalRules += read_16u(data + offset + srcOffset); }
		}
		subtable->rulesCount = totalRules;
		NEW_N(subtable->rules, totalRules);
		uint16_t jj = 0;
		for (uint16_t j = 0; j < chainSubClassSetCnt; j++) {
			uint16_t srcOffset = read_16u(data + offset + 8 + j * 2);
			if (srcOffset) {
				uint16_t srsCount = read_16u(data + offset + srcOffset);
				for (uint16_t k = 0; k < srsCount; k++) {
					uint32_t srOffset = offset + srcOffset + read_16u(data + offset + srcOffset + 2 + k * 2);
					subtable->rules[jj] =
					    GeneralReadContextualRule(data, tableLength, srOffset, j, true, classCoverage, cds);
					jj += 1;
				}
			}
		}
		if (cds && cds->ic && cds->ic->glyphs) free(cds->ic->glyphs);
		if (cds && cds->ic && cds->ic->classes) free(cds->ic->classes);
		if (cds && cds->ic) free(cds->ic);
		return _subtable;
	} else if (format == 3) {
		// Chaining Contextual Substitution Subtable, Coverage based.
		// This table has exactly one rule within it, and i love it.
		subtable->rulesCount = 1;
		NEW_N(subtable->rules, 1);
		subtable->rules[0] = GeneralReadContextualRule(data, tableLength, offset + 2, 0, false, format3Coverage, NULL);
		return _subtable;
	}
FAIL:
	fprintf(stderr, "Unsupported format %d.\n", format);
	DELETE(delete_otl_chaining_subtable, _subtable);
	return NULL;
}

otl_chaining_rule *GeneralReadChainingRule(font_file_pointer data, uint32_t tableLength, uint32_t offset,
                                           uint16_t startGID, bool minusOne, CoverageReaderHandler fn, void *userdata) {
	otl_chaining_rule *rule;
	NEW(rule);
	rule->match = NULL;
	rule->apply = NULL;

	checkLength(offset + 8);
	uint16_t nBack = read_16u(data + offset);
	checkLength(offset + 2 + 2 * nBack + 2);
	uint16_t nInput = read_16u(data + offset + 2 + 2 * nBack);
	checkLength(offset + 4 + 2 * (nBack + nInput - (minusOne ? 1 : 0)) + 2);
	uint16_t nLookaround = read_16u(data + offset + 4 + 2 * (nBack + nInput - (minusOne ? 1 : 0)));
	checkLength(offset + 6 + 2 * (nBack + nInput - (minusOne ? 1 : 0) + nLookaround) + 2);
	uint16_t nApply = read_16u(data + offset + 6 + 2 * (nBack + nInput - (minusOne ? 1 : 0) + nLookaround));
	checkLength(offset + 8 + 2 * (nBack + nInput - (minusOne ? 1 : 0) + nLookaround) + nApply * 4);

	rule->matchCount = nBack + nInput + nLookaround;
	rule->inputBegins = nBack;
	rule->inputEnds = nBack + nInput;

	NEW_N(rule->match, rule->matchCount);
	uint16_t jj = 0;
	for (uint16_t j = 0; j < nBack; j++) {
		uint32_t gid = read_16u(data + offset + 2 + j * 2);
		rule->match[jj++] = fn(data, tableLength, gid, offset, 1, userdata);
	}
	if (minusOne) { rule->match[jj++] = fn(data, tableLength, startGID, offset, 2, userdata); }
	for (uint16_t j = 0; j < nInput - (minusOne ? 1 : 0); j++) {
		uint32_t gid = read_16u(data + offset + 4 + 2 * rule->inputBegins + j * 2);
		rule->match[jj++] = fn(data, tableLength, gid, offset, 2, userdata);
	}
	for (uint16_t j = 0; j < nLookaround; j++) {
		uint32_t gid = read_16u(data + offset + 6 + 2 * (rule->inputEnds - (minusOne ? 1 : 0)) + j * 2);
		rule->match[jj++] = fn(data, tableLength, gid, offset, 3, userdata);
	}
	rule->applyCount = nApply;
	NEW_N(rule->apply, rule->applyCount);
	for (uint16_t j = 0; j < nApply; j++) {
		rule->apply[j].index =
		    rule->inputBegins + read_16u(data + offset + 8 + 2 * (rule->matchCount - (minusOne ? 1 : 0)) + j * 4);
		rule->apply[j].lookupIndex =
		    read_16u(data + offset + 8 + 2 * (rule->matchCount - (minusOne ? 1 : 0)) + j * 4 + 2);
		rule->apply[j].lookupName = NULL;
	}
	reverseBacktracks(rule);
	return rule;

FAIL:
	DELETE(deleteRule, rule);
	return NULL;
}

otl_subtable *caryll_read_chaining(font_file_pointer data, uint32_t tableLength, uint32_t offset) {
	uint16_t format = 0;
	otl_subtable *_subtable;
	NEW(_subtable);
	subtable_chaining *subtable = &(_subtable->chaining);
	subtable->rulesCount = 0;
	subtable->classified = false;
	subtable->bc = NULL;
	subtable->ic = NULL;
	subtable->fc = NULL;
	subtable->rules = NULL;
	checkLength(offset + 2);
	format = read_16u(data + offset);
	if (format == 1) {
		checkLength(offset + 6);
		otl_coverage *firstCoverage = caryll_read_coverage(data, tableLength, offset + read_16u(data + offset + 2));
		uint16_t chainSubRuleSetCount = read_16u(data + offset + 4);
		if (chainSubRuleSetCount != firstCoverage->numGlyphs) goto FAIL;
		checkLength(offset + 6 + 2 * chainSubRuleSetCount);
		uint16_t totalRules = 0;
		for (uint16_t j = 0; j < chainSubRuleSetCount; j++) {
			uint32_t srsOffset = offset + read_16u(data + offset + 6 + j * 2);
			checkLength(srsOffset + 2);
			totalRules += read_16u(data + srsOffset);
			checkLength(srsOffset + 2 + 2 * read_16u(data + srsOffset));
		}
		subtable->rulesCount = totalRules;
		NEW_N(subtable->rules, totalRules);
		uint16_t jj = 0;
		for (uint16_t j = 0; j < chainSubRuleSetCount; j++) {
			uint32_t srsOffset = offset + read_16u(data + offset + 6 + j * 2);
			uint16_t srsCount = read_16u(data + srsOffset);
			for (uint16_t k = 0; k < srsCount; k++) {
				uint32_t srOffset = srsOffset + read_16u(data + srsOffset + 2 + k * 2);
				subtable->rules[jj] = GeneralReadChainingRule(data, tableLength, srOffset, firstCoverage->glyphs[j].gid,
				                                              true, singleCoverage, NULL);
				jj += 1;
			}
		}
		caryll_delete_coverage(firstCoverage);
		return _subtable;
	} else if (format == 2) {
		checkLength(offset + 12);
		classdefs *cds;
		NEW(cds);
		cds->bc = caryll_raad_classdef(data, tableLength, offset + read_16u(data + offset + 4));
		cds->ic = caryll_raad_classdef(data, tableLength, offset + read_16u(data + offset + 6));
		cds->fc = caryll_raad_classdef(data, tableLength, offset + read_16u(data + offset + 8));
		uint16_t chainSubClassSetCnt = read_16u(data + offset + 10);
		checkLength(offset + 12 + 2 * chainSubClassSetCnt);
		uint16_t totalRules = 0;
		for (uint16_t j = 0; j < chainSubClassSetCnt; j++) {
			uint16_t srcOffset = read_16u(data + offset + 12 + j * 2);
			if (srcOffset) { totalRules += read_16u(data + offset + srcOffset); }
		}
		subtable->rulesCount = totalRules;
		NEW_N(subtable->rules, totalRules);
		uint16_t jj = 0;
		for (uint16_t j = 0; j < chainSubClassSetCnt; j++) {
			uint16_t srcOffset = read_16u(data + offset + 12 + j * 2);
			if (srcOffset) {
				uint16_t srsCount = read_16u(data + offset + srcOffset);
				for (uint16_t k = 0; k < srsCount; k++) {
					uint32_t srOffset = offset + srcOffset + read_16u(data + offset + srcOffset + 2 + k * 2);
					subtable->rules[jj] =
					    GeneralReadChainingRule(data, tableLength, srOffset, j, true, classCoverage, cds);
					jj += 1;
				}
			}
		}
		if (cds && cds->bc && cds->bc->glyphs) free(cds->bc->glyphs);
		if (cds && cds->bc && cds->bc->classes) free(cds->bc->classes);
		if (cds && cds->bc) free(cds->bc);
		if (cds && cds->ic && cds->ic->glyphs) free(cds->ic->glyphs);
		if (cds && cds->ic && cds->ic->classes) free(cds->ic->classes);
		if (cds && cds->ic) free(cds->ic);
		if (cds && cds->fc && cds->fc->glyphs) free(cds->fc->glyphs);
		if (cds && cds->fc && cds->fc->classes) free(cds->fc->classes);
		if (cds && cds->fc) free(cds->fc);
		return _subtable;
	} else if (format == 3) {
		// Chaining Contextual Substitution Subtable, Coverage based.
		// This table has exactly one rule within it, and i love it.
		subtable->rulesCount = 1;
		NEW_N(subtable->rules, 1);
		subtable->rules[0] = GeneralReadChainingRule(data, tableLength, offset + 2, 0, false, format3Coverage, NULL);
		return _subtable;
	}
FAIL:
	fprintf(stderr, "Unsupported format %d.\n", format);
	DELETE(delete_otl_chaining_subtable, _subtable);
	return NULL;
}

json_value *caryll_chaining_to_json(otl_subtable *_subtable) {
	subtable_chaining *subtable = &(_subtable->chaining);
	otl_chaining_rule *rule = subtable->rules[0];
	json_value *_st = json_object_new(4);
	json_value *_match = json_array_new(rule->matchCount);
	for (uint16_t j = 0; j < rule->matchCount; j++) {
		json_array_push(_match, caryll_coverage_to_json(rule->match[j]));
	}
	json_value *_apply = json_array_new(rule->applyCount);
	for (uint16_t j = 0; j < rule->applyCount; j++) {
		json_value *_application = json_object_new(2);
		json_object_push(_application, "at", json_integer_new(rule->apply[j].index));
		json_object_push(_application, "lookup", json_string_new(rule->apply[j].lookupName));
		json_array_push(_apply, _application);
	}
	json_object_push(_st, "match", _match);
	json_object_push(_st, "apply", preserialize(_apply));
	json_object_push(_st, "inputBegins", json_integer_new(rule->inputBegins));
	json_object_push(_st, "inputEnds", json_integer_new(rule->inputEnds));
	return _st;
}

otl_lookup *caryll_chaining_from_json(json_value *_lookup, char *_type) {
	otl_lookup *lookup = NULL;
	json_value *_subtables = json_obj_get_type(_lookup, "subtables", json_array);
	if (!_subtables) goto FAIL;

	NEW(lookup);
	lookup->type = strcmp(_type, "gpos_chaining") == 0 ? otl_type_gpos_chaining : otl_type_gsub_chaining;
	lookup->flags = json_obj_getnum(_lookup, "flags");
	lookup->subtableCount = _subtables->u.array.length;
	NEW_N(lookup->subtables, lookup->subtableCount);

	uint16_t jj = 0;
	for (uint16_t j = 0; j < lookup->subtableCount; j++) {
		json_value *_subtable = _subtables->u.array.values[j];
		if (_subtable && _subtable->type == json_object) {
			json_value *_match = json_obj_get_type(_subtable, "match", json_array);
			json_value *_apply = json_obj_get_type(_subtable, "apply", json_array);
			if (_match && _apply) {
				otl_subtable *_st;
				NEW(_st);
				subtable_chaining *subtable = &(_st->chaining);
				subtable->rulesCount = 1;
				subtable->classified = false;
				subtable->bc = NULL;
				subtable->ic = NULL;
				subtable->fc = NULL;
				NEW(subtable->rules);
				NEW(subtable->rules[0]);
				otl_chaining_rule *rule = subtable->rules[0];
				rule->matchCount = _match->u.array.length;
				NEW_N(rule->match, rule->matchCount);
				rule->applyCount = _apply->u.array.length;
				NEW_N(rule->apply, rule->applyCount);
				rule->inputBegins = json_obj_getnum_fallback(_subtable, "inputBegins", 0);
				rule->inputEnds = json_obj_getnum_fallback(_subtable, "inputEnds", rule->matchCount);
				for (uint16_t j = 0; j < rule->matchCount; j++) {
					rule->match[j] = caryll_coverage_from_json(_match->u.array.values[j]);
				}
				for (uint16_t j = 0; j < rule->applyCount; j++) {
					rule->apply[j].index = 0;
					rule->apply[j].lookupIndex = 0;
					rule->apply[j].lookupName = NULL;
					json_value *_application = _apply->u.array.values[j];
					if (_application->type == json_object) {
						json_value *_ln = json_obj_get_type(_application, "lookup", json_string);
						if (_ln) {
							rule->apply[j].lookupName = sdsnewlen(_ln->u.string.ptr, _ln->u.string.length);
							rule->apply[j].index = json_obj_getnum(_application, "at");
						}
					}
				}
				lookup->subtables[jj++] = _st;
			}
		}
	}
	lookup->subtableCount = jj;
	return lookup;
FAIL:
	DELETE(caryll_delete_chaining, lookup);
	return NULL;
}

caryll_buffer *caryll_write_chaining_coverage(otl_subtable *_subtable) {
	caryll_buffer *bufst = bufnew();
	subtable_chaining *subtable = &(_subtable->chaining);
	otl_chaining_rule *rule = subtable->rules[0];
	uint16_t nBacktrack = rule->inputBegins;
	uint16_t nInput = rule->inputEnds - rule->inputBegins;
	uint16_t nLookahead = rule->matchCount - rule->inputEnds;
	uint16_t nSubst = rule->applyCount;
	reverseBacktracks(rule);

	bufwrite16b(bufst, 3); // By coverage
	size_t offset = 10 + 2 * rule->matchCount + 4 * rule->applyCount;
	bufwrite16b(bufst, nBacktrack);
	size_t cp = bufst->cursor;
	for (uint16_t j = 0; j < rule->inputBegins; j++) {
		bufpingpong16b(bufst, caryll_write_coverage(rule->match[j]), &offset, &cp);
	}
	bufwrite16b(bufst, nInput);
	for (uint16_t j = rule->inputBegins; j < rule->inputEnds; j++) {
		bufpingpong16b(bufst, caryll_write_coverage(rule->match[j]), &offset, &cp);
	}
	bufwrite16b(bufst, nLookahead);
	for (uint16_t j = rule->inputEnds; j < rule->matchCount; j++) {
		bufpingpong16b(bufst, caryll_write_coverage(rule->match[j]), &offset, &cp);
	}
	bufwrite16b(bufst, rule->applyCount);
	for (uint16_t j = 0; j < nSubst; j++) {
		bufwrite16b(bufst, rule->apply[j].index - nBacktrack);
		bufwrite16b(bufst, rule->apply[j].lookupIndex);
	}
	return bufst;
}
caryll_buffer *caryll_write_chaining_classes(otl_subtable *_subtable) {
	caryll_buffer *buf = bufnew();
	subtable_chaining *subtable = &(_subtable->chaining);
	// write coverage and class definitions
	caryll_buffer *after = bufnew();
	otl_coverage *coverage;
	NEW(coverage);
	coverage->numGlyphs = subtable->ic->numGlyphs;
	coverage->glyphs = subtable->ic->glyphs;
	bufwrite_bufdel(after, caryll_write_coverage(coverage));
	size_t offsetbc = after->cursor;
	bufwrite_bufdel(after, caryll_write_classdef(subtable->bc));
	size_t offsetic = after->cursor;
	bufwrite_bufdel(after, caryll_write_classdef(subtable->ic));
	size_t offsetfc = after->cursor;
	bufwrite_bufdel(after, caryll_write_classdef(subtable->fc));

	// write rules
	bufwrite16b(buf, 2);                          // by class
	bufwrite16b(buf, 0);                          // coverage offset -- fill later
	bufwrite16b(buf, 0);                          // bc offset -- fill later
	bufwrite16b(buf, 0);                          // ic offset -- fill later
	bufwrite16b(buf, 0);                          // fc offset -- fill later
	bufwrite16b(buf, subtable->ic->maxclass + 1); // nclasses of ic;

	uint16_t *rcpg, totalSets = 0, totalRules = 0;
	NEW_N(rcpg, subtable->ic->maxclass + 1);
	for (uint16_t j = 0; j <= subtable->ic->maxclass; j++) {
		rcpg[j] = 0;
	}
	for (uint16_t j = 0; j < subtable->rulesCount; j++) {
		uint16_t startClass = subtable->rules[j]->match[subtable->rules[j]->inputBegins]->glyphs[0].gid;
		if (startClass <= subtable->ic->maxclass) rcpg[startClass] += 1;
	}

	for (uint16_t j = 0; j <= subtable->ic->maxclass; j++)
		if (rcpg[j]) { totalSets++, totalRules += rcpg[j]; }

	// we are using a three-level ping-pong to maintain this fxxxing table
	size_t setsOffset = buf->cursor + 2 + subtable->ic->maxclass * 2;
	size_t rulesOffset = setsOffset + totalSets * 2 + totalRules * 2;
	for (uint16_t j = 0; j <= subtable->ic->maxclass; j++) {
		if (rcpg[j]) {
			bufwrite16b(buf, setsOffset);
			size_t cp = buf->cursor;
			bufseek(buf, setsOffset);
			bufwrite16b(buf, rcpg[j]);
			for (uint16_t k = 0; k < subtable->rulesCount; k++) {
				otl_chaining_rule *rule = subtable->rules[k];
				uint16_t startClass = rule->match[rule->inputBegins]->glyphs[0].gid;
				if (startClass == j) {
					reverseBacktracks(rule);
					bufwrite16b(buf, rulesOffset - setsOffset);
					size_t xcp = buf->cursor;
					bufseek(buf, rulesOffset);

					uint16_t nBacktrack = rule->inputBegins;
					uint16_t nInput = rule->inputEnds - rule->inputBegins;
					uint16_t nLookahead = rule->matchCount - rule->inputEnds;
					uint16_t nSubst = rule->applyCount;
					
					bufwrite16b(buf, nBacktrack);
					for (uint16_t m = 0; m < rule->inputBegins; m++) {
						bufwrite16b(buf, rule->match[m]->glyphs[0].gid);
					}
					bufwrite16b(buf, nInput);
					for (uint16_t m = rule->inputBegins + 1; m < rule->inputEnds; m++) {
						bufwrite16b(buf, rule->match[m]->glyphs[0].gid);
					}
					bufwrite16b(buf, nLookahead);
					for (uint16_t m = rule->inputEnds; m < rule->matchCount; m++) {
						bufwrite16b(buf, rule->match[m]->glyphs[0].gid);
					}
					bufwrite16b(buf, nSubst);
					for (uint16_t m = 0; m < nSubst; m++) {
						bufwrite16b(buf, rule->apply[m].index - nBacktrack);
						bufwrite16b(buf, rule->apply[m].lookupIndex);
					}

					rulesOffset = buf->cursor;
					bufseek(buf, xcp);
				}
			}
			setsOffset = buf->cursor;
			bufseek(buf, cp);
		} else {
			bufwrite16b(buf, 0);
		}
	}

	// so, how far have we reached?
	size_t afterOffset = buflen(buf);
	bufseek(buf, afterOffset);
	bufwrite_bufdel(buf, after);
	bufseek(buf, 2);
	bufwrite16b(buf, afterOffset);
	bufwrite16b(buf, afterOffset + offsetbc);
	bufwrite16b(buf, afterOffset + offsetic);
	bufwrite16b(buf, afterOffset + offsetfc);

	free(coverage);
	return buf;
}

caryll_buffer *caryll_write_chaining(otl_subtable *_subtable) {
	if (_subtable->chaining.classified) {
		return caryll_write_chaining_classes(_subtable);
	} else {
		return caryll_write_chaining_coverage(_subtable);
	}
}