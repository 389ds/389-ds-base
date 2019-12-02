from typing import List

import pytest

from lib389._mapped_object_lint import (
    DSLint,
    DSLints,
    DSLintMethodSpec
)


def test_dslint():
    class DS(DSLint):
        def lint_uid(self) -> str:
            return self.param

        def __init__(self, param):
            self.param = param
            self.suffixes = ['suffixA', 'suffixB']

        def _lint_nsstate(self, spec: DSLintMethodSpec = None):
            if spec == List:
                yield from self.suffixes
            else:
                to_lint = [spec] if spec else self._lint_nsstate(spec=List)
                for tl in to_lint:
                    if tl == 'suffixA':
                        pass
                    elif tl == 'suffixB':
                        yield 'suffixB is bad'
                    else:
                        raise ValueError('There is no such suffix')

        def _lint_second(self):
            yield from ()

        def _lint_third(self):
            yield from ['this is a fail']

    class DSs(DSLints):
        def list(self):
            for i in [DS("ma"), DS("mb")]:
                yield i

    # single
    inst = DS("a")
    inst_lints = {'nsstate:suffixA', 'nsstate:suffixB', 'second', 'third'}

    assert inst.param == "a"

    assert set(dict(inst.lint_list()).keys()) == inst_lints

    assert set(dict(inst.lint_list('nsstate')).keys()) \
        == {f'nsstate:suffix{s}' for s in "AB"}

    assert list(inst._lint_nsstate(spec=List)) == ['suffixA', 'suffixB']
    assert list(inst.lint()) == ['suffixB is bad', 'this is a fail']

    assert list(inst.lint('nsstate')) == ['suffixB is bad']
    assert list(inst.lint('nsstate:suffixA')) == []
    assert list(inst.lint('nsstate:suffixB')) == ['suffixB is bad']
    with pytest.raises(ValueError):
        list(inst.lint('nonexistent'))

    # multiple
    insts = DSs()

    assert insts.lint_list
    assert insts.lint

    assert set(dict(insts.lint_list()).keys()) \
        == {f'{m}:{s}' for m in ['ma', 'mb'] for s in inst_lints}
    assert set(dict(insts.lint_list('*')).keys()) \
        == {f'{m}:{s}' for m in ['ma', 'mb'] for s in inst_lints}
    assert set(dict(insts.lint_list('*:nsstate')).keys()) \
        == {f'{m}:nsstate:suffix{s}' for m in ['ma', 'mb'] for s in "AB"}
    assert set(dict(insts.lint_list('mb:nsstate')).keys()) \
        == {f'mb:nsstate:suffix{s}' for s in "AB"}
