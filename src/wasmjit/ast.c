/* -*-mode:c; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */

/*
  Copyright (c) 2018 Rian Hunter

  Permission is hereby granted, free of charge, to any person obtaining a copy
  of this software and associated documentation files (the "Software"), to deal
  in the Software without restriction, including without limitation the rights
  to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
  copies of the Software, and to permit persons to whom the Software is
  furnished to do so, subject to the following conditions:

  The above copyright notice and this permission notice shall be included in all
  copies or substantial portions of the Software.

  THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
  IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
  FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
  AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
  LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
  OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
  SOFTWARE.
 */

#include <wasmjit/ast.h>

#include <wasmjit/sys.h>

void init_instruction(struct Instr *instr)
{
	memset(instr, 0, sizeof(*instr));
}

void free_instruction(struct Instr *instr)
{
	switch (instr->opcode) {
	case OPCODE_BLOCK:
	case OPCODE_LOOP: {
		struct BlockLoopExtra *block =
			instr->opcode == OPCODE_BLOCK
			? &instr->data.block : &instr->data.loop;
		if (block->instructions) {
			free_instructions(block->instructions,
					  block->n_instructions);
		}
		break;
	};
	case OPCODE_IF: {
		if (instr->data.if_.instructions_then) {
			free_instructions(instr->data.if_.instructions_then,
					  instr->data.if_.n_instructions_then);
		}
		if (instr->data.if_.instructions_else) {
			free_instructions(instr->data.if_.instructions_else,
					  instr->data.if_.n_instructions_else);
		}
		break;
	}
	case OPCODE_BR_TABLE: {
		if (instr->data.br_table.labelidxs) {
			free(instr->data.br_table.labelidxs);
		}
		break;
	}
	}
}

void free_instructions(struct Instr *instructions, size_t n_instructions)
{
	size_t i;
	for (i = 0; i < n_instructions; ++i) {
		free_instruction(&instructions[i]);
	}
	free(instructions);
}

void init_module(struct Module *module)
{
	memset(module, 0, sizeof(*module));
}

void free_module(struct Module *module)
{
	if (module->type_section.types) {
		free(module->type_section.types);
	}

	if (module->import_section.imports) {
		uint32_t i;
		for (i = 0; i < module->import_section.n_imports; ++i) {
			free(module->import_section.imports[i].name);
			free(module->import_section.imports[i].module);
		}
		free(module->import_section.imports);
	}

	if (module->function_section.typeidxs) {
		free(module->function_section.typeidxs);
	}

	if (module->table_section.tables) {
		free(module->table_section.tables);
	}

	if (module->memory_section.memories) {
		free(module->memory_section.memories);
	}

	if (module->global_section.globals) {
		uint32_t i;
		for (i = 0; i < module->global_section.n_globals; ++i) {
			free_instructions(module->global_section.globals[i].instructions,
					  module->global_section.globals[i].n_instructions);
		}
		free(module->global_section.globals);
	}

	if (module->export_section.exports) {
		uint32_t i;
		for (i = 0; i < module->export_section.n_exports; ++i) {
			if (module->export_section.exports[i].name) {
				free(module->export_section.exports[i].name);
			}
		}
		free(module->export_section.exports);
	}


	if (module->element_section.elements) {
		uint32_t i;
		for (i = 0; i < module->element_section.n_elements; ++i) {
			struct ElementSectionElement *element =
			    &module->element_section.elements[i];

			if (element->instructions) {
				free_instructions(element->instructions,
						  element->n_instructions);
			}

			if (element->funcidxs) {
				free(element->funcidxs);
			}
		}
		free(module->element_section.elements);
	}

	if (module->code_section.codes) {
		uint32_t i;
		for (i = 0; i < module->code_section.n_codes; ++i) {
			struct CodeSectionCode *code = &module->code_section.codes[i];

			if (code->locals) {
				free(code->locals);
			}

			if (code->instructions) {
				free_instructions(code->instructions,
						  code->n_instructions);
			}
		}
		free(module->code_section.codes);
	}

	if (module->data_section.datas) {
		uint32_t i;
		for (i = 0; i < module->data_section.n_datas; ++i) {
			struct DataSectionData *data = &module->data_section.datas[i];

			if (data->instructions) {
				free_instructions(data->instructions,
						  data->n_instructions);
			}

			if (data->buf) {
				free(data->buf);
			}
		}
		free(module->data_section.datas);
	}
}
