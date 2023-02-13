from lib389.utils import DSVersion
import pytest

versions = [('1.3.10.1', '1.3.2.1'),
          ('2.3.2', '1.4.4.4'),
          ('2.3.2.202302121950git1b4f5a5bf', '2.3.2'),
          ('1.1.0a', '1.1.0')]

@pytest.mark.parametrize("x,y", versions)
def test_dsversion(x, y):
    assert DSVersion(x) > DSVersion(y)

