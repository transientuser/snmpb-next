#ifndef TABLENODEVALIDATION_H
#define TABLENODEVALIDATION_H

#include "smi.h"

enum class TableNodeValidation
{
    Valid,
    MissingNode,
    WrongNodeKind,
    MissingRowNode
};

TableNodeValidation ResolveTableRowNode(SmiNode *node,
                                        SmiNode *firstChild,
                                        SmiNode **rowNode);
bool IsValidTableColumnNode(const SmiNode *node);

#endif
