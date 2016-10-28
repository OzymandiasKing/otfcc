#include "libcff.h"

// DICT util functions
void cff_delete_Dict(cff_Dict *dict) {
	if (!dict) return;
	for (uint32_t j = 0; j < dict->count; j++) {
		FREE(dict->ents[j].vals);
	}
	FREE(dict->ents);
	FREE(dict);
}

cff_Dict *cff_extract_Dict(uint8_t *data, uint32_t len) {
	cff_Dict *dict;
	NEW(dict);
	uint32_t index = 0, advance;
	cff_Value val, stack[48];
	uint8_t *temp = data;

	while (temp < data + len) {
		advance = cff_decodeCffToken(temp, &val);

		switch (val.t) {
			case cff_OPERATOR:
				RESIZE(dict->ents, dict->count + 1);
				dict->ents[dict->count].op = val.i;
				dict->ents[dict->count].cnt = index;
				NEW(dict->ents[dict->count].vals, index);
				memcpy(dict->ents[dict->count].vals, stack, sizeof(cff_Value) * index);
				dict->count++;
				index = 0;
				break;
			case cff_INTEGER:
			case cff_DOUBLE:
				stack[index++] = val;
				break;
		}

		temp += advance;
	}

	return dict;
}

void cff_extract_DictByCallback(uint8_t *data, uint32_t len, void *context,
                                void (*callback)(uint32_t op, uint8_t top, cff_Value *stack, void *context)) {
	uint8_t index = 0;
	uint32_t advance;
	cff_Value val, stack[256];
	uint8_t *temp = data;

	while (temp < data + len) {
		advance = cff_decodeCffToken(temp, &val);

		switch (val.t) {
			case cff_OPERATOR:
				callback(val.i, index, stack, context);
				index = 0;
				break;

			case cff_INTEGER:
			case cff_DOUBLE:
				stack[index++] = val;
				break;
		}

		temp += advance;
	}
}

typedef struct {
	bool found;
	cff_Value res;
	uint32_t op;
	uint32_t idx;
} cff_get_key_context;

static void callback_get_key(uint32_t op, uint8_t top, cff_Value *stack, void *_context) {
	cff_get_key_context *context = (cff_get_key_context *)_context;
	if (op == context->op && context->idx <= top) {
		context->found = true;
		context->res = stack[context->idx];
	}
}

cff_Value cff_parseDictKey(uint8_t *data, uint32_t len, uint32_t op, uint32_t idx) {
	cff_get_key_context context;
	context.found = false;
	context.idx = idx;
	context.op = op;
	context.res.t = 0;
	context.res.i = -1;

	cff_extract_DictByCallback(data, len, &context, callback_get_key);
	return context.res;
}

caryll_Buffer *cff_build_Dict(cff_Dict *dict) {
	caryll_Buffer *blob = bufnew();
	for (uint32_t i = 0; i < dict->count; i++) {
		for (uint32_t j = 0; j < dict->ents[i].cnt; j++) {
			caryll_Buffer *blob_val;
			if (dict->ents[i].vals[j].t == cff_INTEGER) {
				blob_val = cff_encodeCffInteger(dict->ents[i].vals[j].i);
			} else if (dict->ents[i].vals[j].t == cff_DOUBLE) {
				blob_val = cff_encodeCffFloat(dict->ents[i].vals[j].d);
			} else {
				blob_val = cff_encodeCffInteger(0);
			}
			bufwrite_bufdel(blob, blob_val);
		}
		bufwrite_bufdel(blob, cff_encodeCffOperator(dict->ents[i].op));
	}
	return blob;
}
