enum {
    TOK_NONE, TOK_OPEN, TOK_CLOSE, TOK_DOT, TOK_QUOTE, TOK_SYM, TOK_NUM,
    TOK_BQ, TOK_COMMA, TOK_COMMAAT, TOK_COMMADOT,
    TOK_SHARPDOT, TOK_LABEL, TOK_BACKREF, TOK_SHARPQUOTE, TOK_SHARPOPEN,
    TOK_OPENB, TOK_CLOSEB, TOK_SHARPSYM, TOK_GENSYM, TOK_DOUBLEQUOTE
};

// defines which characters are ordinary symbol characters.
// exceptions are '.', which is an ordinary symbol character
// unless it's the only character in the symbol, and '#', which is
// an ordinary symbol character unless it's the first character.
static int symchar(char c)
{
    static char *special = "()[]'\";`,\\|";
    return (!isspace(c) && !strchr(special, c));
}

static int isnumtok(char *tok, value_t *pval)
{
    char *end;
    int64_t i64;
    uint64_t ui64;
    double d;
    if (*tok == '\0')
        return 0;
    if (!((tok[0]=='0' && tok[1]=='x') ||  // these formats are always integer
          (tok[0]=='0' && isdigit(tok[1]))) &&
        strpbrk(tok, ".eE")) {
        d = strtod(tok, &end);
        if (*end == '\0') {
            if (pval) *pval = mk_double(d);
            return 1;
        }
        if (end > tok && *end == 'f' && end[1] == '\0') {
            if (pval) *pval = mk_float((float)d);
            return 1;
        }
    }
    if (isdigit(tok[0]) || tok[0]=='-' || tok[0]=='+') {
        if (tok[0]=='-') {
            i64 = strtoll(tok, &end, 0);
            if (pval) *pval = return_from_int64(i64);
        }
        else {
            ui64 = strtoull(tok, &end, 0);
            if (pval) *pval = return_from_uint64(ui64);
        }
        if (*end == '\0')
            return 1;
    }
    return 0;
}

static u_int32_t toktype = TOK_NONE;
static value_t tokval;
static char buf[256];

static char nextchar(FILE *f)
{
    int ch;
    char c;

    do {
        ch = fgetc(f);
        if (ch == EOF)
            return 0;
        c = (char)ch;
        if (c == ';') {
            // single-line comment
            do {
                ch = fgetc(f);
                if (ch == EOF)
                    return 0;
            } while ((char)ch != '\n');
            c = (char)ch;
        }
    } while (isspace(c));
    return c;
}

static void take(void)
{
    toktype = TOK_NONE;
}

static void accumchar(char c, int *pi)
{
    buf[(*pi)++] = c;
    if (*pi >= (int)(sizeof(buf)-1))
        lerror(ParseError, "read: token too long");
}

// return: 1 if escaped (forced to be symbol)
static int read_token(FILE *f, char c, int digits)
{
    int i=0, ch, escaped=0, issym=0, first=1;

    while (1) {
        if (!first) {
            ch = fgetc(f);
            if (ch == EOF)
                goto terminate;
            c = (char)ch;
        }
        first = 0;
        if (c == '|') {
            issym = 1;
            escaped = !escaped;
        }
        else if (c == '\\') {
            issym = 1;
            ch = fgetc(f);
            if (ch == EOF)
                goto terminate;
            accumchar((char)ch, &i);
        }
        else if (!escaped && !(symchar(c) && (!digits || isdigit(c)))) {
            break;
        }
        else {
            accumchar(c, &i);
        }
    }
    ungetc(c, f);
 terminate:
    buf[i++] = '\0';
    return issym;
}

static u_int32_t peek(FILE *f)
{
    char c, *end;
    fixnum_t x;
    int ch;

    if (toktype != TOK_NONE)
        return toktype;
    c = nextchar(f);
    if (feof(f)) return TOK_NONE;
    if (c == '(') {
        toktype = TOK_OPEN;
    }
    else if (c == ')') {
        toktype = TOK_CLOSE;
    }
    else if (c == '[') {
        toktype = TOK_OPENB;
    }
    else if (c == ']') {
        toktype = TOK_CLOSEB;
    }
    else if (c == '\'') {
        toktype = TOK_QUOTE;
    }
    else if (c == '`') {
        toktype = TOK_BQ;
    }
    else if (c == '"') {
        toktype = TOK_DOUBLEQUOTE;
    }
    else if (c == '#') {
        ch = fgetc(f);
        if (ch == EOF)
            lerror(ParseError, "read: invalid read macro");
        if ((char)ch == '.') {
            toktype = TOK_SHARPDOT;
        }
        else if ((char)ch == '\'') {
            toktype = TOK_SHARPQUOTE;
        }
        else if ((char)ch == '\\') {
            u_int32_t cval = u8_fgetc(f);
            if (cval == UEOF)
                lerror(ParseError, "read: end of input in character constant");
            toktype = TOK_NUM;
            tokval = fixnum(cval);
            if (cval > 0x7f) {
                tokval = cvalue_wchar(&tokval, 1);
            }
            else {
                tokval = cvalue_char(&tokval, 1);
            }
        }
        else if ((char)ch == '(') {
            toktype = TOK_SHARPOPEN;
        }
        else if ((char)ch == '<') {
            lerror(ParseError, "read: unreadable object");
        }
        else if (isdigit((char)ch)) {
            read_token(f, (char)ch, 1);
            c = (char)fgetc(f);
            if (c == '#')
                toktype = TOK_BACKREF;
            else if (c == '=')
                toktype = TOK_LABEL;
            else
                lerror(ParseError, "read: invalid label");
            errno = 0;
            x = strtol(buf, &end, 10);
            if (*end != '\0' || errno)
                lerror(ParseError, "read: invalid label");
            tokval = fixnum(x);
        }
        else if ((char)ch == '!') {
            // #! single line comment for shbang script support
            do {
                ch = fgetc(f);
            } while (ch != EOF && (char)ch != '\n');
            return peek(f);
        }
        else if ((char)ch == '|') {
            // multiline comment
            while (1) {
                ch = fgetc(f);
            hashpipe_got:
                if (ch == EOF)
                    lerror(ParseError, "read: eof within comment");
                if ((char)ch == '|') {
                    ch = fgetc(f);
                    if ((char)ch == '#')
                        break;
                    goto hashpipe_got;
                }
            }
            // this was whitespace, so keep peeking
            return peek(f);
        }
        else if ((char)ch == ':') {
            // gensym
            ch = fgetc(f);
            if ((char)ch == 'g')
                ch = fgetc(f);
            read_token(f, (char)ch, 0);
            errno = 0;
            x = strtol(buf, &end, 10);
            if (*end != '\0' || buf[0] == '\0' || errno)
                lerror(ParseError, "read: invalid gensym label");
            toktype = TOK_GENSYM;
            tokval = fixnum(x);
        }
        else if (symchar((char)ch)) {
            read_token(f, ch, 0);
            toktype = TOK_SHARPSYM;
            tokval = symbol(buf);
            c = nextchar(f);
            if (c != '(') {
                take();
                lerror(ParseError, "read: expected argument list for %s",
                       symbol_name(tokval));
            }
        }
        else {
            lerror(ParseError, "read: unknown read macro");
        }
    }
    else if (c == ',') {
        toktype = TOK_COMMA;
        ch = fgetc(f);
        if (ch == EOF)
            return toktype;
        if ((char)ch == '@')
            toktype = TOK_COMMAAT;
        else if ((char)ch == '.')
            toktype = TOK_COMMADOT;
        else
            ungetc((char)ch, f);
    }
    else {
        if (!read_token(f, c, 0)) {
            if (buf[0]=='.' && buf[1]=='\0') {
                return (toktype=TOK_DOT);
            }
            else {
                errno = 0;
                if (isnumtok(buf, &tokval)) {
                    if (errno)
                        lerror(ParseError,"read: overflow in numeric constant");
                    return (toktype=TOK_NUM);
                }
            }
        }
        toktype = TOK_SYM;
        tokval = symbol(buf);
    }
    return toktype;
}

static value_t do_read_sexpr(FILE *f, value_t label);

static value_t read_vector(FILE *f, value_t label, u_int32_t closer)
{
    value_t v=alloc_vector(4, 1), elt;
    u_int32_t i=0;
    PUSH(v);
    if (label != UNBOUND)
        ptrhash_put(&readstate->backrefs, (void*)label, (void*)v);
    while (peek(f) != closer) {
        if (feof(f))
            lerror(ParseError, "read: unexpected end of input");
        if (i >= vector_size(v))
            Stack[SP-1] = vector_grow(v);
        elt = do_read_sexpr(f, UNBOUND);
        v = Stack[SP-1];
        vector_elt(v,i) = elt;
        i++;
    }
    take();
    vector_setsize(v, i);
    return POP();
}

static value_t read_string(FILE *f)
{
    char *buf, *temp;
    char eseq[10];
    size_t i=0, j, sz = 64, ndig;
    int c;
    value_t s;
    u_int32_t wc;

    buf = malloc(sz);
    while (1) {
        if (i >= sz-4) {  // -4: leaves room for longest utf8 sequence
            sz *= 2;
            temp = realloc(buf, sz);
            if (temp == NULL) {
                free(buf);
                lerror(ParseError, "read: out of memory reading string");
            }
            buf = temp;
        }
        c = fgetc(f);
        if (c == EOF) {
            free(buf);
            lerror(ParseError, "read: unexpected end of input in string");
        }
        if (c == '"')
            break;
        else if (c == '\\') {
            c = fgetc(f);
            if (c == EOF) {
                free(buf);
                lerror(ParseError, "read: end of input in escape sequence");
            }
            j=0;
            if (octal_digit(c)) {
                do {
                    eseq[j++] = c;
                    c = fgetc(f);
                } while (octal_digit(c) && j<3 && (c!=EOF));
                if (c!=EOF) ungetc(c, f);
                eseq[j] = '\0';
                wc = strtol(eseq, NULL, 8);
                i += u8_wc_toutf8(&buf[i], wc);
            }
            else if ((c=='x' && (ndig=2)) ||
                     (c=='u' && (ndig=4)) ||
                     (c=='U' && (ndig=8))) {
                wc = c;
                c = fgetc(f);
                while (hex_digit(c) && j<ndig && (c!=EOF)) {
                    eseq[j++] = c;
                    c = fgetc(f);
                }
                if (c!=EOF) ungetc(c, f);
                eseq[j] = '\0';
                if (j) wc = strtol(eseq, NULL, 16);
                i += u8_wc_toutf8(&buf[i], wc);
            }
            else if (c == 'n')
                buf[i++] = '\n';
            else if (c == 't')
                buf[i++] = '\t';
            else if (c == 'r')
                buf[i++] = '\r';
            else if (c == 'b')
                buf[i++] = '\b';
            else if (c == 'f')
                buf[i++] = '\f';
            else if (c == 'v')
                buf[i++] = '\v';
            else if (c == 'a')
                buf[i++] = '\a';
            else
                buf[i++] = c;
        }
        else {
            buf[i++] = c;
        }
    }
    s = cvalue_string(i);
    memcpy(cvalue_data(s), buf, i);
    free(buf);
    return s;
}

// build a list of conses. this is complicated by the fact that all conses
// can move whenever a new cons is allocated. we have to refer to every cons
// through a handle to a relocatable pointer (i.e. a pointer on the stack).
static void read_list(FILE *f, value_t *pval, value_t label)
{
    value_t c, *pc;
    u_int32_t t;

    PUSH(NIL);
    pc = &Stack[SP-1];  // to keep track of current cons cell
    t = peek(f);
    while (t != TOK_CLOSE) {
        if (feof(f))
            lerror(ParseError, "read: unexpected end of input");
        c = mk_cons(); car_(c) = cdr_(c) = NIL;
        if (iscons(*pc)) {
            cdr_(*pc) = c;
        }
        else {
            *pval = c;
            if (label != UNBOUND)
                ptrhash_put(&readstate->backrefs, (void*)label, (void*)c);
        }
        *pc = c;
        c = do_read_sexpr(f,UNBOUND); // must be on separate lines due to
        car_(*pc) = c;                // undefined evaluation order

        t = peek(f);
        if (t == TOK_DOT) {
            take();
            c = do_read_sexpr(f,UNBOUND);
            cdr_(*pc) = c;
            t = peek(f);
            if (feof(f))
                lerror(ParseError, "read: unexpected end of input");
            if (t != TOK_CLOSE)
                lerror(ParseError, "read: expected ')'");
        }
    }
    take();
    (void)POP();
}

// label is the backreference we'd like to fix up with this read
static value_t do_read_sexpr(FILE *f, value_t label)
{
    value_t v, sym, oldtokval, *head;
    value_t *pv;
    u_int32_t t;

    t = peek(f);
    take();
    switch (t) {
    case TOK_CLOSE:
        lerror(ParseError, "read: unexpected ')'");
    case TOK_CLOSEB:
        lerror(ParseError, "read: unexpected ']'");
    case TOK_DOT:
        lerror(ParseError, "read: unexpected '.'");
    case TOK_SYM:
    case TOK_NUM:
        return tokval;
    case TOK_COMMA:
        head = &COMMA; goto listwith;
    case TOK_COMMAAT:
        head = &COMMAAT; goto listwith;
    case TOK_COMMADOT:
        head = &COMMADOT; goto listwith;
    case TOK_BQ:
        head = &BACKQUOTE; goto listwith;
    case TOK_QUOTE:
        head = &QUOTE;
    listwith:
        v = cons_reserve(2);
        car_(v) = *head;
        cdr_(v) = tagptr(((cons_t*)ptr(v))+1, TAG_CONS);
        car_(cdr_(v)) = cdr_(cdr_(v)) = NIL;
        PUSH(v);
        if (label != UNBOUND)
            ptrhash_put(&readstate->backrefs, (void*)label, (void*)v);
        v = do_read_sexpr(f,UNBOUND);
        car_(cdr_(Stack[SP-1])) = v;
        return POP();
    case TOK_SHARPQUOTE:
        // femtoLisp doesn't need symbol-function, so #' does nothing
        return do_read_sexpr(f, label);
    case TOK_OPEN:
        PUSH(NIL);
        read_list(f, &Stack[SP-1], label);
        return POP();
    case TOK_SHARPSYM:
        // constructor notation
        sym = tokval;
        PUSH(NIL);
        read_list(f, &Stack[SP-1], UNBOUND);
        v = POP();
        return apply(symbol_value(sym), v);
    case TOK_OPENB:
        return read_vector(f, label, TOK_CLOSEB);
    case TOK_SHARPOPEN:
        return read_vector(f, label, TOK_CLOSE);
    case TOK_SHARPDOT:
        // eval-when-read
        // evaluated expressions can refer to existing backreferences, but they
        // cannot see pending labels. in other words:
        // (... #2=#.#0# ... )    OK
        // (... #2=#.(#2#) ... )  DO NOT WANT
        v = do_read_sexpr(f,UNBOUND);
        return toplevel_eval(v);
    case TOK_LABEL:
        // create backreference label
        if (ptrhash_has(&readstate->backrefs, (void*)tokval))
            lerror(ParseError, "read: label %ld redefined", numval(tokval));
        oldtokval = tokval;
        v = do_read_sexpr(f, tokval);
        ptrhash_put(&readstate->backrefs, (void*)oldtokval, (void*)v);
        return v;
    case TOK_BACKREF:
        // look up backreference
        v = (value_t)ptrhash_get(&readstate->backrefs, (void*)tokval);
        if (v == (value_t)PH_NOTFOUND)
            lerror(ParseError, "read: undefined label %ld", numval(tokval));
        return v;
    case TOK_GENSYM:
        pv = (value_t*)ptrhash_bp(&readstate->gensyms, (void*)tokval);
        if (*pv == (value_t)PH_NOTFOUND)
            *pv = gensym(NULL, 0);
        return *pv;
    case TOK_DOUBLEQUOTE:
        return read_string(f);
    }
    return NIL;
}

value_t read_sexpr(FILE *f)
{
    value_t v;
    readstate_t state;
    state.prev = readstate;
    ptrhash_new(&state.backrefs, 16);
    ptrhash_new(&state.gensyms, 16);
    readstate = &state;

    v = do_read_sexpr(f, UNBOUND);

    readstate = state.prev;
    free_readstate(&state);
    return v;
}