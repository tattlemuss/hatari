#ifndef SYMBOLTABLEMODEL_H
#define SYMBOLTABLEMODEL_H

#include <QAbstractListModel>
class SymbolTable;

// Structures symbol table for display or auto-completion
class SymbolTableModel : public QAbstractListModel
{
public:
    SymbolTableModel(QObject *parent, const SymbolTable& symbols);
    virtual ~SymbolTableModel() {}

    // Flag that the symbol table has changed.
    // Issue signals so that QCompleter objects don't get broken state.
    void emitChanged();

    virtual int rowCount(const QModelIndex &parent) const;
    virtual QVariant data(const QModelIndex &index, int role = Qt::DisplayRole) const;
    const SymbolTable& m_symbols;
};


#endif // SYMBOLTABLEMODEL_H
