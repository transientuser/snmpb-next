#include "tablenodevalidation.h"

TableNodeValidation ResolveTableRowNode(SmiNode *node,
                                        SmiNode *firstChild,
                                        SmiNode **rowNode)
{
    if (rowNode)
        *rowNode = nullptr;
    if (!node)
        return TableNodeValidation::MissingNode;
    if (node->nodekind == SMI_NODEKIND_ROW)
    {
        if (rowNode)
            *rowNode = node;
        return TableNodeValidation::Valid;
    }
    if (node->nodekind != SMI_NODEKIND_TABLE)
        return TableNodeValidation::WrongNodeKind;
    if (!firstChild || firstChild->nodekind != SMI_NODEKIND_ROW)
        return TableNodeValidation::MissingRowNode;

    if (rowNode)
        *rowNode = firstChild;
    return TableNodeValidation::Valid;
}

bool IsValidTableColumnNode(const SmiNode *node)
{
    return node && node->nodekind == SMI_NODEKIND_COLUMN;
}

bool IsTableQueryCapableNodeKind(SmiNodekind kind)
{
    return kind == SMI_NODEKIND_TABLE || kind == SMI_NODEKIND_ROW;
}
