#include "symboltablemodel.h"
#include "symboltable.h"

SymbolTableModel::SymbolTableModel(QObject *parent, const SymbolTable &symbols) :
    QAbstractListModel(parent),
    m_symbols(symbols)
{
}

void SymbolTableModel::emitChanged()
{
    beginResetModel();
    endResetModel();
}

int SymbolTableModel::rowCount(const QModelIndex & parent) const
{
    if (parent.isValid())
        return 0;
    return (int)m_symbols.Count();
}

QVariant SymbolTableModel::data(const QModelIndex &index, int role) const
{
    if (role == Qt::EditRole || role == Qt::DisplayRole)
    {
        size_t row = index.row();
        if (row < m_symbols.Count())
        {
            return QString(m_symbols.Get(row).name.c_str());
        }
    }
    return QVariant();
}
